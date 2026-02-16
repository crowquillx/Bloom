{
  description = "Bloom - development shell and flake for building the Qt6 Jellyfin client";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable"; # use unstable for latest Qt6
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs { inherit system; };
      qt = pkgs.qt6;
      shell = pkgs.mkShell {
        name = "bloom-devshell";

        buildInputs = with pkgs; [
          cmake
          ninja
          gcc
          git
          pkg-config
          python3
          ccache
          gdb
          qt.qtbase
          qt.qtdeclarative
          qt.qttools
          qt.qtmultimedia
          qt.qtwayland
          qt.qt5compat
          sqlite
          mpv
        ];

        nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];

        # Recommended to set Wayland as platform in the development shell if you are using Wayland
        shellHook = ''
          export QT_QPA_PLATFORM=wayland
          export QML2_IMPORT_PATH=${qt.qtdeclarative}/lib/qt6/qml:$QML2_IMPORT_PATH
          export CMAKE_PREFIX_PATH=${qt.qtbase}/lib/cmake:${qt.qtdeclarative}/lib/cmake:${qt.qttools}/lib/cmake:$CMAKE_PREFIX_PATH
          export QT_PLUGIN_PATH=${qt.qtbase}/lib/qt6/plugins:$QT_PLUGIN_PATH
          echo "Entering Bloom devShell. Use: mkdir -p build && cd build && cmake .. -G Ninja && ninja"
        '';
      };
    in {
      devShells = {
        default = shell;
      };
      packages = rec {
        # Buildable package so users can `nix build .#Bloom`
        Bloom = pkgs.stdenv.mkDerivation {
          pname = "bloom";
          version = "0.3.2";
          src = ./.; # flake root

          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config qt.wrapQtAppsHook ];
          buildInputs = [ qt.qtbase qt.qtdeclarative qt.qttools qt.qtmultimedia qt.qtwayland qt.qt5compat pkgs.sqlite ];

          # Patch CMakeLists.txt for Qt6 6.10+ compatibility
          # Qt6 6.10 changed how qt_add_executable handles relative paths
          # We need to make paths absolute and remove header files
          postPatch = ''
            substituteInPlace src/CMakeLists.txt \
              --replace-fail "qt_add_executable(Bloom
    main.cpp
    core/ServiceLocator.h
    core/ServiceLocator.cpp
    player/PlayerProcessManager.cpp
    player/PlayerController.cpp
    network/JellyfinClient.cpp
    utils/ConfigManager.cpp
    utils/ConfigManager.h
    utils/InputModeManager.cpp
    utils/InputModeManager.h
    viewmodels/LibraryViewModel.h
    viewmodels/LibraryViewModel.cpp
    viewmodels/SeriesDetailsViewModel.h
    viewmodels/SeriesDetailsViewModel.cpp
    ui/ImageCacheProvider.h
    ui/ImageCacheProvider.cpp
)" "qt_add_executable(Bloom
    \''${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/core/ServiceLocator.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/player/PlayerProcessManager.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/player/PlayerController.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/network/JellyfinClient.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/utils/ConfigManager.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/utils/InputModeManager.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/viewmodels/LibraryViewModel.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/viewmodels/SeriesDetailsViewModel.cpp
    \''${CMAKE_CURRENT_SOURCE_DIR}/ui/ImageCacheProvider.cpp
)"
          '';

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          installPhase = ''
            runHook preInstall
            
            mkdir -p $out/bin
            cp src/Bloom $out/bin/
            
            runHook postInstall
          '';

          qtWrapperArgs = [
            # Don't force Wayland, let Qt auto-detect
            # "--set QT_QPA_PLATFORM wayland"
            "--set QML2_IMPORT_PATH ${qt.qtdeclarative}/lib/qt6/qml:${qt.qt5compat}/lib/qt6/qml"
          ];
        };

        # FHS-compatible bundle for distribution
        # This creates a self-contained binary that works on any Linux system
        BloomBundle = pkgs.buildFHSEnv {
          name = "bloom-bundle";
          targetPkgs = pkgs: [
            Bloom
            qt.qtbase
            qt.qtdeclarative
            qt.qttools
            qt.qtmultimedia
            qt.qtwayland
            qt.qt5compat
            pkgs.sqlite
            pkgs.mpv
            # Graphics libraries for OpenGL support
            pkgs.libGL
            pkgs.libglvnd
            pkgs.mesa
            pkgs.vulkan-loader
          ];
          runScript = "Bloom";
          meta.description = "Bloom Jellyfin client (portable bundle)";
        };

        default = Bloom;
      };
    });
}
