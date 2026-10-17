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
# nixpkgs ships none of these yet, so Bloom pins them here. The mpv and
# libplacebo revisions match the Windows SDK recorded in
# packaging/dependencies.json. Both the `mpv` executable used by
# external-mpv-ipc and the libmpv linked by the embedded backend come from the
# single `mpv` derivation below.
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

  ffmpeg9 =
    (ffmpeg.override {
      version = pins.linux_ffmpeg_version;
      # AMF is an AMD encoder SDK (unused by mpv); the locked headers predate
      # what FFmpeg 9 requires.
      withAmf = false;
      source = fetchFromGitHub {
        owner = "FFmpeg";
        repo = "FFmpeg";
        tag = "n${pins.linux_ffmpeg_version}";
        hash = "sha256-/c6cVfMSF6JJxCuJs58bxBlmBXDJ+jbnC4pELB97Qb0=";
      };
    }).overrideAttrs
      (old: {
        # FFmpeg 9 removed the CELT and shaderc wrappers; the locked nixpkgs
        # builder still emits their (disabled) configure switches.
        configureFlags = lib.subtractLists [
          "--disable-libcelt"
          "--disable-libshaderc"
        ] old.configureFlags;
      });

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
