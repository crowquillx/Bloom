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
  src = import ./source.nix { inherit lib; };

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
    "-GNinja"
    (lib.cmakeBool "BUILD_TESTING" false)
    (lib.cmakeBool "BLOOM_BUNDLE_LIBMPV" false)
  ];
  enableParallelBuilding = false;
  buildPhase = ''
    runHook preBuild
    cmake --build . --parallel "$NIX_BUILD_CORES" --target \
      Bloom_copy_qml Bloom_copy_res Bloom_qmltyperegistration
    mapfile -t qml_files < <(find src/BloomUI/ui -type f -name '*.qml' | sort)
    # Keep context-dependent warnings visible while making high-confidence
    # structural defects fail the build.
    qmllint \
      --alias-cycle error \
      --assignment-in-condition error \
      --duplicate-import error \
      --duplicate-property-binding error \
      --inheritance-cycle error \
      --invalid-lint-directive error \
      --unreachable-code error \
      -I src -I ${qt6.qtdeclarative}/lib/qt-6/qml "''${qml_files[@]}"
    runHook postBuild
  '';
  installPhase = ''
    touch "$out"
  '';
}
