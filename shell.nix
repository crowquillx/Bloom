let
  pkgs = import <nixpkgs> { };
  lib = pkgs.lib;
  qt = pkgs.qt6;
in
pkgs.mkShell {
  name = "bloom-devshell";

  packages = with pkgs; [
    ccache
    cmake
    gcc
    gdb
    git
    libsecret
    mpv
    ninja
    pkg-config
    python3
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

    echo "Bloom Nix shell ready."
    echo "  Check deps:    ./scripts/check-deps.sh"
    echo "  Configure:     cmake -B build -G Ninja"
    echo "  Build:         cmake --build build"
    echo "  (Flake users:  nix build .#Bloom)"
  '';
}
