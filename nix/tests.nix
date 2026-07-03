{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  gtest,
  libsecret,
  mpv,
  sqlite,
  qt6,
}:

stdenv.mkDerivation {
  pname = "bloom-tests";
  version = lib.strings.removeSuffix "\n" (builtins.readFile ../VERSION);
  src = lib.cleanSource ../.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    gtest
    libsecret
    mpv
    sqlite
    qt6.qt5compat
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtmultimedia
    qt6.qtshadertools
    qt6.qtsvg
    qt6.qttools
    qt6.qtwayland
  ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" true)
    (lib.cmakeBool "BLOOM_BUNDLE_LIBMPV" false)
    (lib.cmakeBool "BLOOM_BUILD_VISUAL_TESTS" false)
  ];
  enableParallelBuilding = false;
  buildPhase = ''
    runHook preBuild
    cmake --build . --parallel "''${BLOOM_BUILD_JOBS:-2}" --target \
      BaseViewModelTest \
      LibraryCacheStoreTest \
      LibraryItemQueryTest \
      LoggingConfigTest \
      ConfigManagerThemeTest \
      InputBindingManagerTest \
      PlayerBackendFactoryTest \
      PlayerControllerAutoplayContextTest \
      MediaSegmentProviderServiceTest \
      NextEpisodeResolverTest \
      EpisodeSelectionScriptTest \
      TrackPreferencesManagerTest \
      SimilarItemsRetryTest \
      UpNextRecommendationsViewModelTest \
      UpdateServiceTest
    runHook postBuild
  '';
  doCheck = true;
  checkPhase = ''
    runHook preCheck
    export QT_QPA_PLATFORM=offscreen
    ctest --output-on-failure \
      --exclude-regex '^(VisualRegressionTest|SeriesDetailsCacheTest)$'
    runHook postCheck
  '';
  installPhase = ''
    touch "$out"
  '';
}
