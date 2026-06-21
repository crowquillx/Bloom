{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  libsecret,
  mpv,
  sqlite,
  qt6,
}:

stdenv.mkDerivation {
  pname = "bloom-qml-lint";
  version = lib.strings.removeSuffix "\n" (builtins.readFile ../VERSION);
  src = lib.cleanSource ../.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.qttools
    qt6.wrapQtAppsHook
  ];
  buildInputs = [
    libsecret
    mpv
    sqlite
    qt6.qt5compat
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtmultimedia
    qt6.qtshadertools
    qt6.qtsvg
    qt6.qtwayland
  ];
  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" false)
    (lib.cmakeBool "BLOOM_BUNDLE_LIBMPV" false)
  ];
  enableParallelBuilding = false;
  buildPhase = ''
    runHook preBuild
    cmake --build . --parallel "''${BLOOM_BUILD_JOBS:-2}" --target \
      Bloom_copy_qml Bloom_copy_res Bloom_qmltyperegistration
    mapfile -t qml_files < <(find src/BloomUI/ui -type f -name '*.qml' | sort)
    qmllint -I src -I ${qt6.qtdeclarative}/lib/qt-6/qml "''${qml_files[@]}"
    runHook postBuild
  '';
  installPhase = ''
    touch "$out"
  '';
}
