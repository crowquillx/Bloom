#!/usr/bin/env python3
"""Verify that an mpv/libmpv stack provides Dolby Vision Profile 7 FEL.

FEL reconstruction (mpv PR #17932, merge 99b4c12) needs three cooperating
components, and none of them can be inferred from the mpv version alone:

  * mpv with the EL demux split / BL+EL pairing / gpu-next EL upload;
  * FFmpeg with the `dovi_split` bitstream filter (libavcodec >= 62.36.100);
  * libplacebo API >= 370 (mpv silently compiles its FEL renderer path out
    against older libplacebo releases).

Modes (combinable):
  --check-pins          validate packaging/dependencies.json (and optionally the
                        Flatpak manifest / portable build script) so a pre-FEL
                        pin cannot be reintroduced.
  --mpv PATH            run the mpv executable: it must accept
                        `--vf=format=dolbyvision=yes:enhancement-layer=yes` and
                        report libplacebo/libavcodec versions meeting the floor.
  --library PATH        scan built libraries (libmpv-2.dll, libmpv.so,
                        libplacebo.so, libavcodec.so) for code that only exists
                        when each component's FEL support is compiled in.

Exit status is non-zero when any requested check fails.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# Strings that only exist when the corresponding FEL code is compiled in.
MARKERS = {
    # mpv: Profile 7 in-band EL splitter (demux/dovi_split.c, PR #17932).
    "mpv-el-splitter": b"Dolby Vision Profile 7 splitter",
    # mpv: vo_gpu_next EL upload, inside `#if PL_API_VER >= 367`.
    "mpv-gpu-next-el": b"Failed setting up enhancement layer; rendering base layer only.",
    # libplacebo: renderer EL sampling / FEL composition.
    "libplacebo-el": b"Failed sampling enhancement layer",
    # FFmpeg: the dovi_split BSF's own option help text (not mpv's lookup).
    "ffmpeg-dovi-split": b"Which Dolby Vision components to keep in the output bitstream",
}

LIBPLACEBO_VERSION = re.compile(rb"\bv7\.(\d{3})\.(\d+)\b")


class CheckFailure(Exception):
    pass


def version_tuple(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in re.findall(r"\d+", text)[:3])


def libplacebo_api(version: str) -> int:
    match = re.match(r"^v?\d+\.(\d+)", version.strip())
    return int(match.group(1)) if match else -1


def load_dependencies(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def check_pins(deps: dict, flatpak_manifest: Path | None, portable_script: Path | None) -> list[str]:
    errors: list[str] = []
    mpv = deps.get("mpv", {})
    fel = deps.get("dolby_vision_fel", {})

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    merge = fel.get("mpv_merge_commit", "")
    require(re.fullmatch(r"[0-9a-f]{40}", merge) is not None,
            "dolby_vision_fel.mpv_merge_commit must be a full commit hash")
    merged_at = fel.get("mpv_merged_at", "").replace("-", "")
    min_api = int(fel.get("libplacebo_min_api", 0))
    require(min_api >= 370, "dolby_vision_fel.libplacebo_min_api must be >= 370")
    min_lavc = version_tuple(fel.get("libavcodec_min_version", "0"))
    require(min_lavc >= (62, 36, 100), "dolby_vision_fel.libavcodec_min_version must be >= 62.36.100")
    min_ffmpeg = version_tuple(fel.get("ffmpeg_min_release", "0"))
    require(min_ffmpeg >= (9, 0), "dolby_vision_fel.ffmpeg_min_release must be >= 9.0")

    revision = mpv.get("source_revision", "")
    require(re.fullmatch(r"[0-9a-f]{40}", revision) is not None,
            "mpv.source_revision must be a full commit hash")
    describe = mpv.get("describe", "")
    require(describe.startswith("v") and revision[:9] in describe,
            "mpv.describe must be the `git describe` of mpv.source_revision")

    tag = mpv.get("windows_release_tag", "")
    require(re.fullmatch(r"\d{8}", tag) is not None and tag >= merged_at,
            f"Windows SDK release {tag!r} predates the FEL merge ({merged_at})")
    sdk_version = mpv.get("windows_sdk_version", "")
    require(sdk_version.startswith(f"{tag}-git-") and revision.startswith(sdk_version.split("-git-")[-1]),
            "mpv.windows_sdk_version must be <release>-git-<mpv.source_revision prefix>")
    require(re.fullmatch(r"[0-9a-f]{64}", mpv.get("windows_sdk_sha256", "")) is not None,
            "mpv.windows_sdk_sha256 must be a SHA-256 hex digest")
    archive = f"mpv-dev-x86_64-{sdk_version}.7z"
    urls = mpv.get("windows_sdk_urls", [])
    require(bool(urls), "mpv.windows_sdk_urls must list at least one download URL")
    for url in urls:
        require(url.startswith("https://") and url.endswith("/" + archive),
                f"Windows SDK URL must be HTTPS and name {archive}: {url}")

    stack = mpv.get("windows_stack", {})
    require(stack.get("mpv") == describe, "mpv.windows_stack.mpv must equal mpv.describe")
    require(libplacebo_api(stack.get("libplacebo", "")) >= min_api,
            "Windows SDK libplacebo API is below the FEL floor")
    require(re.fullmatch(r"[0-9a-f]{40}", stack.get("ffmpeg_revision", "")) is not None,
            "mpv.windows_stack.ffmpeg_revision must pin the FFmpeg commit")

    require(libplacebo_api(mpv.get("libplacebo_version", "")) >= min_api,
            "mpv.libplacebo_version is below the FEL floor")
    require(re.fullmatch(r"[0-9a-f]{40}", mpv.get("libplacebo_revision", "")) is not None,
            "mpv.libplacebo_revision must be a full commit hash")
    require(version_tuple(mpv.get("linux_ffmpeg_version", "0")) >= min_ffmpeg,
            "mpv.linux_ffmpeg_version predates the dovi_split BSF")

    if flatpak_manifest is not None:
        text = flatpak_manifest.read_text(encoding="utf-8")
        require(revision in text, "Flatpak manifest does not build mpv at mpv.source_revision")
        require(mpv.get("libplacebo_revision", "") in text,
                "Flatpak manifest does not build libplacebo at mpv.libplacebo_revision")
        ffmpeg_version = mpv.get('linux_ffmpeg_version', '')
        require(f"ffmpeg-{ffmpeg_version}.tar" in text or f"/n{ffmpeg_version}" in text,
                "Flatpak manifest does not build FFmpeg at mpv.linux_ffmpeg_version")
        require("v0.41.0.tar.gz" not in text, "Flatpak manifest still references the mpv 0.41.0 release")
        zimg = deps.get("zimg", {})
        require(bool(zimg.get("version")) and re.fullmatch(r"[0-9a-f]{64}", zimg.get("sha256", "")) is not None,
                "zimg source pin is required for HDR seek previews")
        require(f"release-{zimg.get('version')}.tar.gz" in text
                and zimg.get("sha256", "missing") in text and "--enable-libzimg" in text,
                "Flatpak must build the pinned zimg and enable FFmpeg zscale")
        require("/app/bin/bloom-seek-preview --self-test" in text,
                "Flatpak must check the deployed seek preview helper")

    if portable_script is not None:
        text = portable_script.read_text(encoding="utf-8")
        portable = deps.get("portable", {})
        require("--executable /build/stage/usr/bin/bloom-seek-preview" in text
                and '"$APPDIR/usr/bin/bloom-seek-preview" --self-test' in text,
                "portable build must deploy and check the seek preview helper")
        uses_distribution_mpv = re.search(r"\blibmpv-dev\b", text) is not None
        if portable.get("fel_capable", False):
            require(not uses_distribution_mpv,
                    "portable.fel_capable is true but the build installs the distribution libmpv-dev")
            require("build-mpv.sh" in text and "--executable /opt/bloom-mpv/bin/mpv" in text,
                    "portable build must compile and deploy the pinned mpv executable")
            require("--require-fel --require-pin" in text,
                    "portable build must reject an insufficient deployed stack")
        else:
            # A target that cannot ship the FEL stack must say why and must
            # report the bundled runtime's capability during packaging.
            require(bool(portable.get("fel_blockers")),
                    "portable.fel_capable is false without documented portable.fel_blockers")
            require("check-mpv-fel-capability.py" in text,
                    "portable build must report the bundled mpv FEL capability")
    return errors


def check_executable(path: Path, deps: dict, require_pin: bool) -> tuple[dict, list[str]]:
    fel = deps.get("dolby_vision_fel", {})
    errors: list[str] = []
    command = [str(path), "--no-config", "--vf=format=dolbyvision=yes:enhancement-layer=yes", "--version"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"executable": str(path)}, [f"failed to run {path}: {error}"]

    output = result.stdout + result.stderr
    if result.returncode != 0:
        # Old mpv rejects the option before printing versions; collect them
        # anyway so the report says which stack is in use.
        try:
            plain = subprocess.run([str(path), "--no-config", "--version"], capture_output=True,
                                   text=True, timeout=30, check=False)
            output = plain.stdout + plain.stderr
        except (OSError, subprocess.TimeoutExpired):
            pass
    report: dict = {"executable": str(path), "accepts_enhancement_layer": result.returncode == 0}
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("mpv "):
            report["mpv"] = line.split(" Copyright")[0]
        elif line.startswith("libplacebo version:"):
            report["libplacebo"] = line.split(":", 1)[1].strip()
        elif line.startswith("FFmpeg version:"):
            report["ffmpeg"] = line.split(":", 1)[1].strip()
        elif line.startswith("libavcodec"):
            runtime = re.search(r"\(runtime (\d+\.\d+\.\d+)\)", line)
            report["libavcodec"] = runtime.group(1) if runtime else line.split()[1]

    if result.returncode != 0:
        errors.append("mpv rejected vf=format enhancement-layer (pre-PR-#17932 mpv)")
    if libplacebo_api(report.get("libplacebo", "")) < int(fel.get("libplacebo_min_api", 370)):
        errors.append(f"libplacebo {report.get('libplacebo', 'unknown')} is below API "
                      f"{fel.get('libplacebo_min_api')}")
    if version_tuple(report.get("libavcodec", "0")) < version_tuple(fel.get("libavcodec_min_version", "0")):
        errors.append(f"libavcodec {report.get('libavcodec', 'unknown')} lacks the dovi_split BSF")
    if require_pin:
        describe = deps.get("mpv", {}).get("describe", "")
        if report.get("mpv") != f"mpv {describe}":
            errors.append(f"mpv reports {report.get('mpv')!r}, expected pinned 'mpv {describe}'")
    return report, errors


def scan_libraries(paths: list[Path], deps: dict) -> tuple[dict, list[str]]:
    fel = deps.get("dolby_vision_fel", {})
    found: dict[str, list[str]] = {name: [] for name in MARKERS}
    placebo_apis: set[int] = set()
    errors: list[str] = []
    for path in paths:
        try:
            data = path.read_bytes()
        except OSError as error:
            errors.append(f"cannot read {path}: {error}")
            continue
        for name, marker in MARKERS.items():
            if marker in data:
                found[name].append(str(path))
        placebo_apis.update(int(match.group(1)) for match in LIBPLACEBO_VERSION.finditer(data))

    for name, locations in found.items():
        if not locations:
            errors.append(f"no library contains the {name} code ({MARKERS[name].decode()!r})")
    report = {"markers": found, "libplacebo_api_strings": sorted(placebo_apis)}
    if not placebo_apis:
        errors.append("embedded libplacebo API version is unavailable")
    elif max(placebo_apis) < int(fel.get("libplacebo_min_api", 370)):
        errors.append(f"embedded libplacebo version strings {sorted(placebo_apis)} are below the FEL floor")
    return report, errors


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dependencies", type=Path, required=True)
    parser.add_argument("--check-pins", action="store_true")
    parser.add_argument("--flatpak-manifest", type=Path)
    parser.add_argument("--portable-script", type=Path)
    parser.add_argument("--mpv", type=Path)
    parser.add_argument("--library", type=Path, action="append", default=[])
    parser.add_argument("--require-fel", action="store_true",
                        help="fail unless the executable/libraries prove FEL support")
    parser.add_argument("--require-pin", action="store_true",
                        help="the executable must report the pinned mpv describe string")
    args = parser.parse_args(argv)
    if not (args.check_pins or args.mpv or args.library):
        parser.error("select --check-pins, --mpv, or --library")
    if args.require_pin and not args.mpv:
        parser.error("--require-pin requires --mpv")

    deps = load_dependencies(args.dependencies)
    report: dict = {}
    errors: list[str] = []
    if args.require_fel and not args.library:
        errors.append("compiled FEL support requires library evidence; provide --library paths")

    if args.check_pins:
        pin_errors = check_pins(deps, args.flatpak_manifest, args.portable_script)
        report["pins"] = "ok" if not pin_errors else pin_errors
        errors += pin_errors
    if args.mpv:
        report["executable"], executable_errors = check_executable(args.mpv, deps, args.require_pin)
        if args.require_fel or args.require_pin:
            errors += executable_errors
        report["executable"]["fel_errors"] = executable_errors
    if args.library:
        report["libraries"], library_errors = scan_libraries(args.library, deps)
        if args.require_fel:
            errors += library_errors
        report["libraries"]["fel_errors"] = library_errors

    print(json.dumps(report, indent=2, sort_keys=True))
    if errors:
        print("Dolby Vision FEL capability check FAILED:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    if any(value.get("fel_errors") for value in report.values() if isinstance(value, dict)):
        print("Dolby Vision FEL capability is insufficient; see the report.", file=sys.stderr)
    else:
        print("Dolby Vision FEL capability check passed.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
