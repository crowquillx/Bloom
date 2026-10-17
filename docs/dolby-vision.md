# Dolby Vision processing and Profile 7 FEL

Bloom preserves its existing HDR output policy. Dolby Vision source processing
is separate from display HDR state. A PQ picture on the display does not prove
that mpv used the RPU or reconstructed the full enhancement layer, or FEL.

## Dependency audit and pins

Before this change, no packaged target provided the complete upstream FEL stack.

| Target | Previous dependency | Previous supporting libraries | Result |
| --- | --- | --- | --- |
| Windows | shinchiro `20260610-git-304426c` | June FFmpeg/libplacebo snapshots | Predates FEL. The archive was no longer available at the upstream URL during the audit. |
| Nix | nixpkgs mpv `0.41.0` | FFmpeg `8.1`, libplacebo `7.360.1` | No FEL splitter or renderer support. |
| Portable AppImage, Debian, tar | Ubuntu 24.04 mpv `0.37` and `libmpv-dev` | FFmpeg `6.1`, libplacebo `6.338` | No FEL support. |
| Flatpak | mpv `0.41.0` source, libplacebo `7.360.1` module | KDE 6.10 runtime FFmpeg | No complete FEL stack. |

The minimum mpv feature baseline is [PR #17932](https://github.com/mpv-player/mpv/pull/17932),
merge `99b4c12cccb4d8d3f72b41944cb6c640e2156650`, merged July 1, 2026.
The build must also contain FFmpeg's `dovi_split` bitstream filter and the
libplacebo enhancement-layer renderer. A version string or source commit alone
does not establish these capabilities.

The pinned mpv code has an
[EL splitter](https://github.com/mpv-player/mpv/blob/dde0671072723c311df874a32dc4e48b5197ca62/demux/dovi_split.c)
that requires `dovi_split`. FFmpeg introduced that filter at libavcodec
`62.36.100`; FFmpeg 9.0 is the first release that includes it. The
[gpu-next path](https://github.com/mpv-player/mpv/blob/dde0671072723c311df874a32dc4e48b5197ca62/video/out/vo_gpu_next.c)
needs libplacebo API 367 for the EL frame and API 370 for its metadata mapping.
Bloom requires API 370 or newer. mpv can build against libplacebo `7.360.1`
while omitting this renderer path.

`packaging/dependencies.json` is the dependency record. The current pins are:

| Component | Exact pin |
| --- | --- |
| mpv, all packaged targets | `dde0671072723c311df874a32dc4e48b5197ca62`, describe `v0.41.0-1200-gdde067107` |
| Windows SDK | shinchiro `mpv-dev-x86_64-20261017-git-dde0671072.7z` |
| Windows SDK SHA-256 | `d6638b2b40561e6071627f39ad60a38e15721dc87a49f244b897911ea2edfd9f` |
| Windows FFmpeg | `N-127602-g2d49d0e62`, commit `2d49d0e62438507f40409a4d5ca978563202dcf3` |
| Windows libplacebo | `v7.376.0 (v7.360.0-171-g0df80ae-dirty)`, as reported by the DLL |
| Linux libplacebo | `0df80ae5229e8e520c0fa4e3d666098a3ffa1cbc`, API 376 |
| Linux FFmpeg | `9.0.2` |
| Nix package | `mpv-0.41.0-unstable-2026-10-16`, defined in `nix/mpv-stack.nix` |

Windows uses the headers, import library, and DLL from one SDK extraction.
Both download URLs require the same archive SHA-256. The Bloom mirror URL is a
retention fallback; its presence in the manifest does not mean an asset has
been published there. The upstream archive has limited retention. Before it
expires, a maintainer must preserve the exact archive at the mirror URL or
update the pin. Bloom does not download an unspecified latest release.

Windows CI passes `-RequireFelCapableMpv`. The build checks the DLL for the
pinned describe string and compiled FEL code. Packaging checks the staged DLL
again and writes `mpv-provenance.json`, including its SHA-256 and stack pins.
`-AllowUnverifiedMpv` is a local packaging escape hatch and is not used by CI.
The shinchiro DLL statically includes FFmpeg and libplacebo. The recorded
libplacebo describe string has a `dirty` suffix, so its exact bytes are defined
by the SDK archive hash, rather than by the upstream commit alone.

Nix uses one mpv derivation for both the external executable and linked
libmpv. The Bloom wrapper puts that package on `PATH`. Development shells,
tests, and QML checks receive the same derivation. The flake does not update
all of nixpkgs to obtain this stack. Qt Multimedia may load its own FFmpeg
version; its versioned symbols and different sonames remain separate from
libmpv's FFmpeg.

Portable Linux builds the same mpv/libplacebo/FFmpeg pins inside the digest-pinned
Ubuntu 24.04 container. It builds Wayland `1.23.1`, wayland-protocols `1.45`,
and Vulkan headers/loader `vulkan-sdk-1.4.309.0` to meet current mpv build
requirements. It explicitly bundles the Wayland client libraries and Vulkan
loader that linuxdeploy normally excludes. Host GPU drivers and Vulkan ICDs
remain external. Both the deployed mpv executable and deployed libraries must
pass the FEL checks before an artifact is produced. The default executable
lookup prefers `mpv` beside Bloom, including extracted tar packages. An explicit
user path remains an override and receives the runtime checks below.

Flatpak builds the pinned mpv, FFmpeg 9.0.2, and libplacebo under `/app`.
It does not use the runtime's older mpv or FFmpeg for playback. Its existing
OpenGL build policy remains in place; Vulkan is disabled in this manifest.
The canonical manifest is `packaging/flatpak/com.github.crowquillx.Bloom.yml`.
Use `nix run .#package-flatpak` to build it.

## Playback behavior

| Source | Behavior |
| --- | --- |
| SDR | No added DV filter and no new HDR output request. |
| HDR10 or HLG | Existing HDR classification, metadata, output switching, and local tone mapping. |
| DV Profile 5 | Existing unsupported-profile output policy. mpv/gpu-next can still reshape DV before local SDR tone mapping. |
| DV Profile 7 MEL | RPU processing. An EL composition pass is not required for a MEL stream. |
| DV Profile 7 FEL | Preserve the EL, allow BL/EL pairing, and let gpu-next/libplacebo compose it when the runtime supports it. |
| DV Profile 8 | RPU processing with the compatible base layer. |

The provider projection supplies DV profile and compatibility hints. A file
name containing `FEL` or `MEL` is a hint only. Bloom does not infer successful
FEL reconstruction from a profile number or file name.

For a DV source and a runtime that accepts the modern filter, Bloom adds:

```text
--vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes
```

The labelled entry follows the profile and user arguments. Its label prevents
duplicate Bloom entries. It preserves existing filters and GLSL shaders.
Embedded backends replay `vf` replacements and list edits in their original
order through mpv's `vf` command after initialization. They do not pass list
suffixes to `mpv_set_option_string` as independent options.

Older mpv receives `format=dolbyvision=yes` when it accepts that option. A
runtime that accepts neither option receives no Bloom DV filter. Playback
continues with a diagnostic. A user filter that removes metadata before the
Bloom entry can still lose information; setting `yes` later cannot restore
it. Bloom reports explicit `dolbyvision=no` and `enhancement-layer=no` filters.

`enableHDR`, `hdr_output_mode`, match-content behavior, DV fallback mode,
display switching order, hardware-decoder selection, and profile render API
selection keep their existing meaning. The safe metadata default is still
`target-colorspace-hint-mode=target`. `source-dynamic` remains opt-in. HDR-to-SDR
output retains DV processing before tone mapping. This change does not request
native Dolby Vision HDMI signaling or set a fixed peak luminance.

## Backend limits

| Backend | DV/FEL rendering |
| --- | --- |
| Windows `win-libmpv` | Uses gpu-next and the verified DLL. Display, driver, and decoder behavior still require Windows runtime validation. |
| `external-mpv-ipc` | Uses the selected executable. gpu-next can apply DV/FEL with a compatible stack. Older or custom runtimes can have less support. |
| Experimental `linux-libmpv-opengl` | Links the new libmpv, but its current `vo=libmpv` render API uses the legacy renderer. It cannot apply DV reshaping or FEL. Bloom reports this renderer limit. |

Linux Wayland still defaults to external mpv. Embedded Linux remains an
explicit validation option, and its existing fallback behavior is unchanged.
Supporting FEL within that render API needs upstream renderer support or a
separate backend change. This change does not replace Bloom's backend design.

## Runtime checks and diagnostics

`MpvRuntimeProbe` resolves the external executable and runs bounded,
asynchronous probes with `--no-config --version`. It first tests both format
options, then DV-only, then a plain version query. Each stage has a deadline
and a bounded output buffer. It does not wait for a child on the UI thread.

Embedded backends test the linked parser with a temporary, uninitialized mpv
handle. After initialization, they record mpv, FFmpeg, and libplacebo properties,
the client API version, and the loaded library path where the OS exposes it.
The verified packaged describe can identify the build-time checked embedded
stack. Unverified FFmpeg versions that meet the floor remain unknown until
library inspection or playback supplies evidence. A version below the floor
is insufficient.

Bloom uses `bloom.playback` logging. It records one runtime summary and
asynchronous startup/steady snapshots. Detailed SDR snapshots use debug
logging. HDR/DV snapshots include:

- Backend, executable or loaded library path, and runtime versions.
- Actual VO/context, requested GPU API and hardware decoder, selected hardware
  decoder, and possible software fallback.
- HDR output mode, effective colorspace hints, and the effective `vf` chain.
- DV profile, codec/decoder, bit depth, transfer function, primaries, output
  parameters, and drop/delay counters where mpv exposes them.
- RPU delivery from the `dolbyvision` output colormatrix and EL composition
  from an `enhancement layer` render pass in `vo-passes`.

Production does not enable `show-dependent-tracks`, because that option changes
track visibility and selection. Thus the EL track can be unobservable. mpv does
not independently expose the selected EL decoder through these properties.
Missing decoder, filter-chain, or renderer metadata properties remain
unavailable. Decoded DV metadata alone does not confirm delivery to the renderer.
Software fallback alone is not
a failure, and no GPU vendor is required. A render pass proves EL composition
for the observed frame; a `fel=yes` capability summary alone does not.

## Developer verification

Do not add sample media to the repository. Use a known Profile 7 FEL file or
remux that you can use legally. Run the exact packaged mpv:

```zsh
mpv_package=$(nix build --no-link --print-out-paths '.#mpv^out')
nix develop --command python3 -I scripts/verify-dolby-vision-fel.py \
  --mpv "$mpv_package/bin/mpv" --sample /path/to/p7-fel.mkv \
  --output /tmp/bloom-fel-check --start 30 --seconds 8 --output-mode sdr
```

Repeat with `--output-mode hdr` on an HDR display. `--mpv-arg=--gpu-context=...`
can select a context for the test. `--hwdec=no` tests software decoding.
Windows can run the same Python script against a matching mpv executable build;
the DLL-only SDK is not an executable. This script verifies upstream rendering,
so also inspect Bloom's own logs with the same source and selected backend.

The script writes `report.json`, logs, and screenshots for EL-on and EL-off.
IPC reply deadlines apply to both Unix sockets and Windows named pipes. A silent
or disconnected peer stops the run and enters child-process cleanup.
It requires Profile 7 detection, BL/EL identification, pairing and renderer
composition evidence, the effective EL flag, preserved DV metadata, the
selected output transfer, and observable performance counters. It rejects
EL fallback messages. It disables dithering/debanding, seeks both runs to the
same frame, and requires different decoded screenshots plus loss of the EL
render pass when EL is disabled. Missing evidence fails the check. A selected
frame with no visible FEL contribution can fail the pixel comparison; retain
the evidence and test another known FEL frame instead of treating that as a pass.

For dependency checks without media:

```zsh
nix build .#checks.x86_64-linux.mpv-fel-capability \
  .#checks.x86_64-linux.dependency-pins
```

The checker tests option parsing and version floors, then scans the actual
libraries for the compiled mpv splitter, gpu-next EL upload, libplacebo EL
sampling, and FFmpeg `dovi_split` implementation. This is build capability
evidence. It does not replace a real FEL playback test. Binary markers are
specific to the pinned sources; update the checks when upstream changes them.

The test results and unavailable hardware checks are recorded in
[manual validation](manual-validation.md#dolby-vision-fel-upgrade).

## Files changed for this upgrade

The obsolete root Flatpak manifest was removed. The other files contain the
implementation, tests, dependency pins, or documentation for this upgrade.

```text
.github/workflows/ci.yml
.github/workflows/update-dependencies.yml
AGENTS.md
README.md
THIRD_PARTY_NOTICES.md
com.github.crowquillx.Bloom.yml
docs/build.md
docs/dolby-vision.md
docs/manual-validation.md
docs/playback.md
flake.nix
nix/mpv-stack.nix
nix/source.nix
packaging/dependencies.json
packaging/flatpak/com.github.crowquillx.Bloom.yml
packaging/portable/build-mpv.sh
packaging/portable/build.sh
scripts/MpvFelCapability.ps1
scripts/build.ps1
scripts/check-mpv-fel-capability.py
scripts/package-flatpak.sh
scripts/package-windows.ps1
scripts/verify-dolby-vision-fel.py
src/CMakeLists.txt
src/cmake/BloomLibraries.cmake
src/player/DolbyVisionPolicy.cpp
src/player/DolbyVisionPolicy.h
src/player/MpvRuntimeProbe.cpp
src/player/MpvRuntimeProbe.h
src/player/PlayerController.cpp
src/player/PlayerController.h
src/player/PlayerProcessManager.cpp
src/player/PlayerProcessManager.h
src/player/backend/ExternalMpvBackend.cpp
src/player/backend/ExternalMpvBackend.h
src/player/backend/IPlayerBackend.h
src/player/backend/LinuxMpvBackend.cpp
src/player/backend/LinuxMpvBackend.h
src/player/backend/MpvLibmpvRuntime.cpp
src/player/backend/MpvLibmpvRuntime.h
src/player/backend/MpvNodeUtils.h
src/player/backend/WindowsMpvBackend.cpp
src/player/backend/WindowsMpvBackend.h
tests/CMakeLists.txt
tests/ConfigManagerThemeTest.cpp
tests/MpvFelVerificationTest.py
tests/PlaybackPolicyTest.cpp
tests/PlayerBackendFactoryTest.cpp
tests/PlayerProcessManagerTest.cpp
```
