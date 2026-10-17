#!/usr/bin/env python3
"""Developer verification of Dolby Vision Profile 7 FEL reconstruction.

Plays a user-supplied Profile 7 FEL sample twice with the same mpv:

  el-on   --vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes
  el-off  --vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer=no

and proves, from structured mpv properties (JSON IPC) plus the verbose mpv
log, that the EL path is actually used -- not merely that the video played:

  1. mpv detected Profile 7                       track-list dolby-vision-profile
  2. BL and EL identified                         dependent EL track / splitter log
  3. EL paired with the BL                        EL composited, no BL-only pairing
  4. enhancement-layer=yes effective              effective `vf` chain
  5. gpu-next/libplacebo received the EL          vo-passes "enhancement layer"
  6. DV metadata not stripped                     video-out-params colormatrix
  7. valid PQ/HDR or tone-mapped output           video-target-params
  8. no fallback discarded the EL                 log: "base layer only"
  9. realtime / decoder fallback reported         drop counters, hwdec-current

The el-off run must lose the "enhancement layer" render pass, and the two
screenshots of the same frame must differ, demonstrating that the toggle
changes the Profile 7 rendering path. No media is bundled with Bloom; pass
your own (legally obtained) sample with --sample.

Use the mpv Bloom actually ships, e.g. on Nix:
    scripts/verify-dolby-vision-fel.py --mpv "$(nix build --no-link --print-out-paths '.#mpv^out')/bin/mpv" \\
        --sample ~/samples/p7-fel.mkv --output /tmp/fel-report
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

PROPERTIES = [
    "mpv-version", "ffmpeg-version", "libplacebo-version",
    "current-vo", "current-gpu-context", "options/gpu-api",
    "options/hwdec", "hwdec-current", "track-list", "vf",
    "options/target-colorspace-hint", "options/target-colorspace-hint-mode",
    "video-dec-params", "video-out-params", "video-target-params", "vo-passes",
    "frame-drop-count", "decoder-frame-drop-count", "vo-delayed-frame-count",
    "estimated-vf-fps", "container-fps", "display-fps",
]

LOG_EVIDENCE = {
    "splitter": "Dolby Vision Profile 7 splitter",
    "dovi_split_missing": "'dovi_split' BSF not available",
    "el_decoder_failed": "Failed to set up enhancement-layer decoder",
    "el_pair_failed": "Failed to set up enhancement-layer pairing",
    "el_upload_failed": "Failed setting up enhancement layer",
    "el_sampling_failed": "Failed sampling enhancement layer",
    "bl_emitted_alone": "emitting BL",
    "hwdec_fallback": "Falling back to software decoding",
    "hwdec_unsupported": "Could not create device",
}


class IpcClient:
    def __init__(self, path: str, timeout: float = 20.0):
        deadline = time.monotonic() + timeout
        self._buffer = b""
        self._next_id = 1
        while True:
            try:
                if os.name == "nt":
                    self._pipe = open(path, "r+b", buffering=0)  # noqa: SIM115
                    self._sock = None
                else:
                    self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    self._sock.connect(path)
                    self._sock.settimeout(timeout)
                return
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.1)

    def _write(self, data: bytes) -> None:
        if self._sock:
            self._sock.sendall(data)
        else:
            self._pipe.write(data)

    def _read(self) -> bytes:
        return self._sock.recv(65536) if self._sock else self._pipe.read(65536)

    def command(self, *args, timeout: float = 15.0):
        request_id = self._next_id
        self._next_id += 1
        self._write(json.dumps({"command": list(args), "request_id": request_id}).encode() + b"\n")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while b"\n" in self._buffer:
                line, self._buffer = self._buffer.split(b"\n", 1)
                try:
                    message = json.loads(line)
                except ValueError:
                    continue
                if message.get("request_id") == request_id:
                    if message.get("error") != "success":
                        return None
                    return message.get("data")
            chunk = self._read()
            if not chunk:
                break
            self._buffer += chunk
        return None

    def close(self) -> None:
        try:
            if self._sock:
                self._sock.close()
            else:
                self._pipe.close()
        except OSError:
            pass


def decode_png(path: Path) -> tuple[int, int, int, list[float]] | None:
    """Minimal PNG reader (8/16-bit RGB/RGBA, non-interlaced) -> luma-ish samples."""
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return None
    pos, idat, header = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    if not header:
        return None
    width, height, depth, color, _, _, interlace = header
    channels = {2: 3, 6: 4}.get(color)
    if channels is None or depth not in (8, 16) or interlace:
        return None
    bpp = channels * depth // 8
    stride = width * bpp
    raw = zlib.decompress(idat)
    rows, previous = [], bytearray(stride)
    for y in range(height):
        filter_type = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            up = previous[i]
            upper_left = previous[i - bpp] if i >= bpp else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + up) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else upper_left)
                line[i] = (line[i] + predictor) & 0xFF
        rows.append(bytes(line))
        previous = line
    scale = 255.0 if depth == 8 else 65535.0
    samples: list[float] = []
    step = max(1, width // 256)  # subsample for speed
    for line in rows[::step]:
        for x in range(0, width, step):
            offset = x * bpp
            if depth == 8:
                r, g, b = line[offset], line[offset + 1], line[offset + 2]
            else:
                r, g, b = struct.unpack(">HHH", line[offset:offset + 6])
            samples.append((0.2627 * r + 0.6780 * g + 0.0593 * b) / scale)
    return width, height, depth, samples


def run_mode(mpv: str, sample: str, mode: str, args: argparse.Namespace, output: Path) -> dict:
    enhancement = "yes" if mode == "el-on" else "no"
    log_path = output / f"{mode}.log"
    shot_path = output / f"{mode}.png"
    log_path.unlink(missing_ok=True)
    shot_path.unlink(missing_ok=True)
    if os.name == "nt":
        ipc_path = rf"\\.\pipe\bloom-fel-verify-{os.getpid()}-{mode}"
    else:
        ipc_path = str(Path(tempfile.mkdtemp(prefix="bloom-fel-")) / "ipc.sock")
    command = [
        mpv, "--no-config", "--idle=no", "--keep-open=yes", "--vo=gpu-next",
        f"--hwdec={args.hwdec}", "--show-dependent-tracks=yes",
        "--msg-level=all=v", f"--log-file={log_path}", f"--input-ipc-server={ipc_path}",
        f"--start={args.start}", "--screenshot-format=png", "--screenshot-high-bit-depth=no",
        # Deterministic rendering so any el-on/el-off pixel difference comes
        # from the enhancement layer, not from temporal dithering noise.
        "--dither=no", "--deband=no",
        f"--vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer={enhancement}",
    ]
    if args.output_mode == "hdr":
        command += ["--target-colorspace-hint=auto", "--target-colorspace-hint-mode=target"]
    else:
        command += ["--target-colorspace-hint=no", "--tone-mapping=auto", "--hdr-compute-peak=auto"]
    command += args.mpv_arg + [sample]

    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result: dict = {"mode": mode, "command": command}
    client = None
    try:
        client = IpcClient(ipc_path)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            position = client.command("get_property", "time-pos")
            if isinstance(position, (int, float)) and position >= args.start + 1.0:
                break
            time.sleep(0.2)
        window_start = time.monotonic()
        position_start = client.command("get_property", "time-pos")
        time.sleep(args.seconds)
        position_end = client.command("get_property", "time-pos")
        elapsed = time.monotonic() - window_start
        result["playback-window"] = {"wall-seconds": elapsed, "position-start": position_start,
                                     "position-end": position_end}
        if all(isinstance(p, (int, float)) for p in (position_start, position_end)):
            ratio = (position_end - position_start) / elapsed
            result["playback-window"].update({"media-seconds-per-wall-second": ratio,
                                              "realtime": 0.9 <= ratio <= 1.1})
        result["properties"] = {name: client.command("get_property", name) for name in PROPERTIES}
        client.command("set_property", "pause", True)
        client.command("seek", args.start + args.screenshot_offset, "absolute+exact")
        time.sleep(1.5)
        result["screenshot-position"] = client.command("get_property", "time-pos")
        client.command("screenshot-to-file", str(shot_path), "video")
        time.sleep(1.0)
        client.command("quit")
    except OSError as error:
        result["error"] = f"IPC failed: {error}"
    finally:
        if client:
            client.close()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        if os.name != "nt":
            Path(ipc_path).unlink(missing_ok=True)
            Path(ipc_path).parent.rmdir()

    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    result["log"] = {name: log_text.count(needle) for name, needle in LOG_EVIDENCE.items()}
    result["screenshot"] = str(shot_path) if shot_path.exists() else None
    return result


def evaluate(run: dict) -> dict:
    props = run.get("properties") or {}
    tracks = props.get("track-list") or []
    selected = next((t for t in tracks if t.get("type") == "video" and t.get("selected")
                     and not t.get("dependent")), {})
    dependent = [t for t in tracks if t.get("type") == "video" and t.get("dependent")]
    passes = props.get("vo-passes") or {}
    descriptions = [p.get("desc", "") for kind in ("fresh", "redraw") for p in passes.get(kind, [])]
    vf_chain = json.dumps(props.get("vf"))
    out_params = props.get("video-out-params") or {}
    target = props.get("video-target-params") or {}
    log = run.get("log", {})
    hwdec = props.get("hwdec-current") or "no"
    return {
        "1_profile7_detected": selected.get("dolby-vision-profile") == 7,
        "2_bl_el_identified": bool(dependent) or log.get("splitter", 0) > 0,
        "3_el_paired": any("enhancement layer" in d for d in descriptions)
                       and log.get("el_pair_failed", 0) == 0 and log.get("el_decoder_failed", 0) == 0,
        "4_enhancement_layer_flag": "enhancement-layer" in vf_chain
                                    and '"enhancement-layer": "yes"' in vf_chain,
        "5_renderer_received_el": any("enhancement layer" in d for d in descriptions),
        "6_dv_metadata_preserved": out_params.get("colormatrix") == "dolbyvision",
        "7_output": {"gamma": target.get("gamma"), "primaries": target.get("primaries"),
                     "sig-peak": target.get("sig-peak")},
        "8_no_el_fallback": all(log.get(key, 0) == 0 for key in
                                ("dovi_split_missing", "el_decoder_failed", "el_pair_failed",
                                 "el_upload_failed", "el_sampling_failed", "bl_emitted_alone")),
        "9_performance": {
            "playback-window": run.get("playback-window"),
            "hwdec-requested": props.get("options/hwdec"),
            "hwdec-current": hwdec,
            "software-decoding": hwdec in ("", "no"),
            "hwdec-fallback-log-lines": log.get("hwdec_fallback", 0),
            "frame-drop-count": props.get("frame-drop-count"),
            "decoder-frame-drop-count": props.get("decoder-frame-drop-count"),
            "vo-delayed-frame-count": props.get("vo-delayed-frame-count"),
            "estimated-vf-fps": props.get("estimated-vf-fps"),
            "container-fps": props.get("container-fps"),
            "bl-frames-emitted-without-el": log.get("bl_emitted_alone", 0),
        },
        "el_decoder": "not exposed by mpv (paired internally by f_enhancement_pair)",
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mpv", default="mpv")
    parser.add_argument("--sample", required=True)
    parser.add_argument("--output", type=Path, default=Path("fel-verification"))
    parser.add_argument("--seconds", type=float, default=8.0, help="realtime window per run")
    parser.add_argument("--start", type=float, default=30.0, help="start position (seconds)")
    parser.add_argument("--screenshot-offset", type=float, default=2.0)
    parser.add_argument("--hwdec", default="auto-safe", help="mpv --hwdec (Bloom profile default)")
    parser.add_argument("--output-mode", choices=("hdr", "sdr"), default="sdr",
                        help="hdr: Bloom match-content hint; sdr: Bloom tone-map-to-sdr path")
    parser.add_argument("--mpv-arg", action="append", default=[], help="extra mpv argument")
    args = parser.parse_args(argv)
    if args.seconds <= 0 or args.start < 0 or args.screenshot_offset < 0:
        parser.error("seconds must be positive; start and screenshot-offset must be non-negative")
    args.output.mkdir(parents=True, exist_ok=True)

    runs = {mode: run_mode(args.mpv, args.sample, mode, args, args.output) for mode in ("el-on", "el-off")}
    report = {mode: {"evidence": evaluate(run), **run} for mode, run in runs.items()}

    on, off = report["el-on"]["evidence"], report["el-off"]["evidence"]
    comparison = {"el-off-lost-el-pass": on["5_renderer_received_el"] and not off["5_renderer_received_el"]}
    shots = [runs[m].get("screenshot") for m in ("el-on", "el-off")]
    if all(shots):
        decoded = [decode_png(Path(s)) for s in shots]
        if all(decoded) and decoded[0][:2] == decoded[1][:2] and len(decoded[0][3]) == len(decoded[1][3]):
            diffs = [abs(a - b) for a, b in zip(decoded[0][3], decoded[1][3])]
            comparison["screenshot-mean-abs-diff"] = sum(diffs) / len(diffs)
            comparison["screenshot-max-abs-diff"] = max(diffs)
            comparison["screenshots-differ"] = max(diffs) > 0.0
    positions = [runs[m].get("screenshot-position") for m in ("el-on", "el-off")]
    comparison["same-frame-position"] = all(isinstance(p, (int, float)) for p in positions) \
        and abs(positions[0] - positions[1]) < 0.001
    report["comparison"] = comparison

    (args.output / "report.json").write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")
    required = [k for k in ("1_profile7_detected", "2_bl_el_identified", "3_el_paired",
                            "4_enhancement_layer_flag", "5_renderer_received_el",
                            "6_dv_metadata_preserved", "8_no_el_fallback") if not on[k]]
    if on["5_renderer_received_el"] and off["5_renderer_received_el"]:
        required.append("el-off run still composed the EL (toggle had no effect)")
    if comparison.get("screenshots-differ") is not True:
        required.append("el-on/el-off screenshots are missing, unreadable, or identical")
    if not comparison["same-frame-position"]:
        required.append("el-on/el-off screenshots are not at the same frame position")
    output_gamma = on["7_output"]["gamma"]
    valid_gammas = {"pq", "hlg"} if args.output_mode == "hdr" else {"srgb", "bt.1886", "gamma2.2", "gamma2.4"}
    if output_gamma not in valid_gammas or not on["7_output"]["primaries"]:
        required.append(f"selected {args.output_mode} output was not observed")
    if not all(isinstance(on["9_performance"].get(key), (int, float)) for key in
               ("frame-drop-count", "decoder-frame-drop-count", "vo-delayed-frame-count")):
        required.append("playback performance counters are unavailable")

    print(json.dumps({"el-on": on, "el-off": off, "comparison": comparison}, indent=2, default=str))
    print(f"Full report: {args.output / 'report.json'}", file=sys.stderr)
    if required:
        print("FEL verification FAILED: " + "; ".join(required), file=sys.stderr)
        return 1
    print("FEL verification passed.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
