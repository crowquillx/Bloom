{
  description = "Bloom - reproducible development, build, test, and release tooling";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          bloom = pkgs.callPackage ./nix/package.nix { };
        in
        {
          inherit bloom;
          Bloom = bloom;
          default = bloom;
        }
      );

      apps = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          bloom = self.packages.${system}.bloom;
          mkApp = name: text: {
            type = "app";
            program = "${
              pkgs.writeShellApplication {
                inherit name;
                runtimeInputs = with pkgs; [
                  appstream
                  bash
                  bubblewrap
                  coreutils
                  curl
                  docker-client
                  findutils
                  flatpak
                  flatpak-builder
                  git
                  jq
                  librsvg
                  nix
                  podman
                  python3
                  skopeo
                ];
                runtimeEnv.GDK_PIXBUF_MODULE_FILE = "${pkgs.librsvg}/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache";
                inherit text;
              }
            }/bin/${name}";
          };
        in
        {
          default = {
            type = "app";
            program = "${bloom}/bin/bloom";
          };
          bloom = self.apps.${system}.default;
          package-linux = mkApp "package-linux" ''
            exec ${./scripts/package-linux-portable.sh} "$@"
          '';
          package-flatpak = mkApp "package-flatpak" ''
            exec ${./scripts/package-flatpak.sh} "$@"
          '';
          verify-artifacts = mkApp "verify-artifacts" ''
            exec ${./scripts/verify-release-artifacts.sh} "$@"
          '';
        }
      );

      checks = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          bloom = self.packages.${system}.bloom;
        in
        {
          inherit bloom;
          tests = pkgs.callPackage ./nix/tests.nix { };
          qml-lint = pkgs.callPackage ./nix/qml-lint.nix { };
          release-smoke =
            pkgs.runCommand "bloom-release-smoke"
              {
                nativeBuildInputs = [ pkgs.gnugrep ];
              }
              ''
                export HOME="$TMPDIR/home"
                export XDG_CACHE_HOME="$TMPDIR/cache"
                mkdir -p "$HOME" "$XDG_CACHE_HOME"
                export FONTCONFIG_FILE=${pkgs.fontconfig.out}/etc/fonts/fonts.conf
                export LC_ALL=C.UTF-8
                export QT_QPA_PLATFORM=offscreen
                output="$(${bloom}/bin/bloom --version 2>&1)"
                printf '%s\n' "$output"
                test "$output" = "Bloom ${bloom.version}"
                help_output="$(${bloom}/bin/bloom --help 2>&1)"
                printf '%s\n' "$help_output" | grep -F -- "--verbose, -v"
                touch "$out"
              '';
          automation-lint =
            pkgs.runCommand "bloom-automation-lint"
              {
                nativeBuildInputs = with pkgs; [
                  actionlint
                  shellcheck
                ];
              }
              ''
                shellcheck \
                  ${./scripts/run-clang-tidy.sh} \
                  ${./scripts/run-coverage.sh} \
                  ${./scripts/run-sanitizers.sh}
                actionlint \
                  ${./.github/workflows/ci.yml} \
                  ${./.github/workflows/deep-analysis.yml} \
                  ${./.github/workflows/update-dependencies.yml}
                touch "$out"
              '';
          metadata =
            pkgs.runCommand "bloom-metadata-check"
              {
                nativeBuildInputs = with pkgs; [
                  appstream
                  desktop-file-utils
                ];
              }
              ''
                desktop-file-validate ${./src/resources/linux/com.github.crowquillx.Bloom.desktop}
                appstreamcli validate --no-net ${./src/resources/linux/com.github.crowquillx.Bloom.metainfo.xml}
                touch "$out"
              '';
          release-manifest =
            pkgs.runCommand "bloom-release-manifest-check"
              {
                nativeBuildInputs = [ pkgs.jq ];
              }
              ''
                jq -e '
                  .schema == 1 and
                  (.qt.version | type == "string") and
                  (.mpv.version | type == "string") and
                  (.portable.image | startswith("docker://")) and
                  (.flatpak.sdk_commit | length == 64) and
                  (.flatpak.platform_commit | length == 64) and
                  (.gtest.version | type == "string") and
                  (.gtest.commit | length == 40)
                ' ${./packaging/dependencies.json} >/dev/null
                touch "$out"
              '';
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          bloom = self.packages.${system}.bloom;
          commonPackages = with pkgs; [
            appstream
            cachix
            ccache
            desktop-file-utils
            flatpak
            flatpak-builder
            gdb
            git
            jq
            nixfmt
            podman
            python3
            skopeo
          ];
          commonShellArgs = {
            inputsFrom = [ bloom ];
            packages = commonPackages;
            shellHook = ''
              export QT_PLUGIN_PATH="${
                pkgs.lib.makeSearchPath "lib/qt-6/plugins" [
                  pkgs.qt6.qtbase
                  pkgs.qt6.qtdeclarative
                  pkgs.qt6.qtimageformats
                  pkgs.qt6.qtmultimedia
                  pkgs.qt6.qtsvg
                  pkgs.qt6.qtwayland
                ]
              }''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
              export QML2_IMPORT_PATH="${
                pkgs.lib.makeSearchPath "lib/qt-6/qml" [
                  pkgs.qt6.qt5compat
                  pkgs.qt6.qtbase
                  pkgs.qt6.qtdeclarative
                  pkgs.qt6.qtmultimedia
                  pkgs.qt6.qtsvg
                  pkgs.qt6.qtwayland
                ]
              }''${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
              export QT_QUICK_CONTROLS_STYLE="''${QT_QUICK_CONTROLS_STYLE:-Basic}"
              export QML_DISABLE_DISK_CACHE="''${QML_DISABLE_DISK_CACHE:-1}"
              export QT_MEDIA_BACKEND="''${QT_MEDIA_BACKEND:-ffmpeg}"
              echo "Bloom development shell"
              echo "  build:    nix build"
              echo "  run:      nix run"
              echo "  validate: nix flake check"
              echo "  package:  nix run .#package-linux -- --output dist"
            '';
          };
        in
        {
          default = pkgs.mkShell (commonShellArgs // { name = "bloom"; });
          analysis = pkgs.mkShell (
            commonShellArgs
            // {
              name = "bloom-analysis";
              packages = commonPackages ++ [
                pkgs.clang-tools
                pkgs.gcovr
              ];
              shellHook = commonShellArgs.shellHook + ''
                export BLOOM_ANALYSIS_SHELL=1
              '';
            }
          );
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt-tree);
    };
}
