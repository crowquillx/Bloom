with import <nixpkgs> {};

let
  qt = qt6;
in
mkShell {
  name = "bloom-devshell";

  buildInputs = [
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
    # qt.qtquickcontrols2 may not exist in some nixpkgs — Quick Controls are runtime QML modules
    qt.qttools
    qt.qtgraphicaleffects
    sqlite
    mpv
  ];

  nativeBuildInputs = [ cmake ninja ];

  shellHook = let
    qmlPaths = "${qt.qtdeclarative}/lib/qt6/qml:${qt.qtgraphicaleffects}/lib/qt6/qml";
  in ''
    export QT_QPA_PLATFORM=wayland
    export QML2_IMPORT_PATH=${qmlPaths}:$QML2_IMPORT_PATH
    export CMAKE_PREFIX_PATH=${qt.qtbase}/lib/cmake:${qt.qtdeclarative}/lib/cmake:${qt.qttools}/lib/cmake:$CMAKE_PREFIX_PATH
    export QT_PLUGIN_PATH=${qt.qtbase}/lib/qt6/plugins:$QT_PLUGIN_PATH
    echo "To build: mkdir -p build && cd build && cmake .. -G Ninja && ninja"
  '';
}
