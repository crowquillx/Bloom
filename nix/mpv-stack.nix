# Pinned mpv/libmpv stack with Dolby Vision Profile 7 FEL support.
#
# Upstream FEL reconstruction (mpv PR #17932, merge commit 99b4c12) needs all
# three of:
#   * mpv containing 99b4c12 (EL demux split, BL/EL pairing, gpu-next upload),
#   * FFmpeg with the `dovi_split` BSF (libavcodec >= 62.36.100; FFmpeg 9.0+),
#   * libplacebo API >= 370 (pl_frame.enhancement_layer is API 367; FEL
#     metadata mapping became unconditional in API 370). mpv still builds
#     against libplacebo 7.360.1, but then silently compiles the FEL renderer
#     path out, so the libplacebo pin matters as much as the mpv pin.
#
# nixpkgs ships FFmpeg 9 but not these mpv and libplacebo revisions, so Bloom
# pins those here; they match the Windows SDK recorded in
# packaging/dependencies.json. nixpkgs' FFmpeg must match the
# `linux_ffmpeg_version` pin shared with the portable and Flatpak builds. Both
# the `mpv` executable used by external-mpv-ipc and the libmpv linked by the
# embedded backend come from the single `mpv` derivation below.
{
  lib,
  fetchFromGitHub,
  ffmpeg,
  libplacebo,
  mpv-unwrapped,
}:

let
  pins = (lib.importJSON ../packaging/dependencies.json).mpv;
  fel = (lib.importJSON ../packaging/dependencies.json).dolby_vision_fel;

  # Use nixpkgs' FFmpeg unmodified so it substitutes from the binary cache.
  # When nixpkgs moves past the pin, bump `linux_ffmpeg_version` (and the
  # portable/Flatpak builds) together instead of overriding the source here.
  ffmpeg9 =
    assert lib.assertMsg (ffmpeg.version == pins.linux_ffmpeg_version)
      "nixpkgs FFmpeg ${ffmpeg.version} does not match mpv.linux_ffmpeg_version ${pins.linux_ffmpeg_version}";
    ffmpeg;

  libplaceboFel = libplacebo.overrideAttrs (_: {
    version = pins.libplacebo_version;
    src = fetchFromGitHub {
      owner = "haasn";
      repo = "libplacebo";
      rev = pins.libplacebo_revision;
      hash = "sha256-NVNDdkBop62ZXZd0r5n8RJb/pfqYvEtZUuFTT260SyY=";
    };
  });

  mpv =
    (mpv-unwrapped.override {
      ffmpeg = ffmpeg9;
      libplacebo = libplaceboFel;
    }).overrideAttrs
      (old: {
        version = pins.version_nix;
        src = fetchFromGitHub {
          owner = "mpv-player";
          repo = "mpv";
          rev = pins.source_revision;
          hash = "sha256-e1+5J6daBdOnFv6FoRbWVt8BblRqtrOaRdZJxp6u6Xo=";
        };

        # Tarballs carry no git metadata; record the exact `git describe` so
        # `mpv --version` and the `mpv-version` property identify the pin.
        postPatch = ''
          printf '%s\n' '${lib.removePrefix "v" pins.describe}' > MPV_VERSION
          sed -i "s|^conf_data.set_quoted('CONFIGURATION', .*|conf_data.set_quoted('CONFIGURATION', '<omitted>')|" meson.build
          grep -qF "conf_data.set_quoted('CONFIGURATION', '<omitted>')" meson.build
          pushd TOOLS
          mv mpv_identify.sh mpv_identify
          patchShebangs *.py *.sh
          mv mpv_identify mpv_identify.sh
          popd
        '';

        # The upstream version check expects a release tag string. Replace it
        # with a capability check: the built mpv must accept the FEL option and
        # report the pinned libplacebo/FFmpeg stack.
        dontVersionCheck = true;
        postInstallCheck = (old.postInstallCheck or "") + ''
          out_text="$("$out/bin/mpv" --no-config \
            --vf=format=dolbyvision=yes:enhancement-layer=yes --version)"
          printf '%s\n' "$out_text"
          printf '%s\n' "$out_text" | grep -F -- "${pins.describe}"
          placebo_api="$(printf '%s\n' "$out_text" \
            | sed -n 's/^libplacebo version: v[0-9]*\.\([0-9]*\)\..*/\1/p')"
          test "''${placebo_api:-0}" -ge ${toString fel.libplacebo_min_api}
          grep -aqF dovi_split "${lib.getLib ffmpeg9}/lib/libavcodec.so"
        '';

        passthru = (old.passthru or { }) // {
          ffmpeg = ffmpeg9;
          libplacebo = libplaceboFel;
        };
      });
in
{
  inherit mpv;
  ffmpeg = ffmpeg9;
  libplacebo = libplaceboFel;
}
