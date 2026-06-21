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
                runtimeEnv.GDK_PIXBUF_MODULE_FILE =
                  "${pkgs.librsvg}/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache";
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
        in
        {
          default = pkgs.mkShell {
            name = "bloom";
            inputsFrom = [ bloom ];
            packages = with pkgs; [
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
            shellHook = ''
              export QT_QUICK_CONTROLS_STYLE="''${QT_QUICK_CONTROLS_STYLE:-Basic}"
              export QML_DISABLE_DISK_CACHE="''${QML_DISABLE_DISK_CACHE:-1}"
              echo "Bloom development shell"
              echo "  build:    nix build"
              echo "  run:      nix run"
              echo "  validate: nix flake check"
              echo "  package:  nix run .#package-linux -- --output dist"
            '';
          };
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt-tree);
    };
}
