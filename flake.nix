{
  description = "Bloom - Nix development shell and Linux package";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;
        qt = pkgs.qt6;

        bloomBuildInputs = with pkgs; [
          libsecret
          mpv
          sqlite
          qt.qt5compat
          qt.qtbase
          qt.qtdeclarative
          qt.qtmultimedia
          qt.qtshadertools
          qt.qtsvg
          qt.qttools
          qt.qtwayland
        ];

        bloomNativeBuildInputs = with pkgs; [
          cmake
          makeWrapper
          ninja
          pkg-config
          qt.wrapQtAppsHook
        ];

        bloomDevTools = with pkgs; [
          ccache
          gcc
          gdb
          git
          python3
        ];

        bloomPackage = pkgs.stdenv.mkDerivation rec {
          pname = "bloom";
          version = "0.5.2";
          src = lib.cleanSource ./.;

          strictDeps = true;

          nativeBuildInputs = bloomNativeBuildInputs;
          buildInputs = bloomBuildInputs;

          qtWrapperArgs = [
            "--set-default"
            "QML_DISABLE_DISK_CACHE"
            "1"
            "--set-default"
            "QT_AUDIO_BACKEND"
            "pulseaudio"
            "--set-default"
            "QT_MEDIA_BACKEND"
            "ffmpeg"
          ];

          cmakeFlags = [
            "-DBUILD_TESTING=OFF"
          ];

          postFixup = ''
            rm -f $out/lib/bloom/libmpv.so
            mv "$out/bin/Bloom" "$out/bin/.Bloom-qt-wrapped"
            makeWrapper "$out/bin/.Bloom-qt-wrapped" "$out/bin/Bloom" \
              --unset LD_LIBRARY_PATH \
              --unset GIO_EXTRA_MODULES \
              --unset GSETTINGS_SCHEMA_DIR \
              --unset GSETTINGS_BACKEND \
              --unset QT_PLUGIN_PATH \
              --unset QML2_IMPORT_PATH \
              --unset QML_IMPORT_PATH \
              --unset NIXPKGS_QT6_QML_IMPORT_PATH
          '';

          meta = with lib; {
            description = "Jellyfin HTPC client built with Qt 6/QML and mpv";
            homepage = "https://github.com/crowquillx/Bloom";
            license = licenses.gpl3Only;
            mainProgram = "Bloom";
            platforms = platforms.linux;
          };
        };
      in
      {
        devShells.default = pkgs.mkShell {
          name = "bloom-devshell";
          inputsFrom = [ bloomPackage ];
          packages = bloomDevTools;

          shellHook = ''
            unset LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH NIXPKGS_QT6_QML_IMPORT_PATH
            export NIXPKGS_QT6_QML_IMPORT_PATH=${qt.qtdeclarative}/lib/qt-6/qml:${qt.qt5compat}/lib/qt-6/qml:${qt.qtmultimedia}/lib/qt-6/qml:${qt.qtwayland}/lib/qt-6/qml
            export QML2_IMPORT_PATH=$NIXPKGS_QT6_QML_IMPORT_PATH
            export QML_IMPORT_PATH=$NIXPKGS_QT6_QML_IMPORT_PATH
            export CMAKE_PREFIX_PATH=${
              lib.concatStringsSep ":" [
                "${qt.qtbase}/lib/cmake"
                "${qt.qtdeclarative}/lib/cmake"
                "${qt.qtmultimedia}/lib/cmake"
                "${qt.qtshadertools}/lib/cmake"
                "${qt.qttools}/lib/cmake"
              ]
            }''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}
            export QT_PLUGIN_PATH=${qt.qtbase}/lib/qt-6/plugins:${qt.qtdeclarative}/lib/qt-6/plugins:${qt.qtmultimedia}/lib/qt-6/plugins:${qt.qtsvg}/lib/qt-6/plugins:${qt.qtwayland}/lib/qt-6/plugins
            export QT_QUICK_CONTROLS_STYLE=''${QT_QUICK_CONTROLS_STYLE:-Basic}
            export QML_DISABLE_DISK_CACHE=''${QML_DISABLE_DISK_CACHE:-1}
            export QT_AUDIO_BACKEND=''${QT_AUDIO_BACKEND:-pulseaudio}
            export QT_MEDIA_BACKEND=''${QT_MEDIA_BACKEND:-ffmpeg}
            unset GIO_EXTRA_MODULES GSETTINGS_SCHEMA_DIR GSETTINGS_BACKEND

            if [ "''${XDG_SESSION_TYPE:-}" = "wayland" ] && [ -z "''${QT_QPA_PLATFORM:-}" ]; then
              export QT_QPA_PLATFORM=wayland
            fi

            echo "Bloom Nix dev shell ready."
            echo "  Check deps: ./scripts/check-deps.sh"
            echo "  Build app:  nix build .#Bloom"
          '';
        };

        packages = rec {
          Bloom = bloomPackage;

          BloomBundle = pkgs.buildFHSEnv {
            name = "bloom-bundle";
            targetPkgs = _: bloomBuildInputs ++ [ bloomPackage ];
            runScript = "Bloom";

            meta = {
              description = "Bloom Jellyfin client (FHS runtime wrapper)";
            };
          };

          default = Bloom;
        };

        checks = {
          build = bloomPackage;
        };
      }
    );
}
