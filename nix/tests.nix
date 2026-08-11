{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  python3,
  gtest,
  libsecret,
  mpv,
  sqlite,
  qt6,
}:

stdenv.mkDerivation {
  pname = "bloom-tests";
  version = lib.strings.removeSuffix "\n" (builtins.readFile ../VERSION);
  src = import ./source.nix {
    inherit lib;
    includeTests = true;
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    python3
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
    qt6.qtimageformats
    qt6.qtmultimedia
    qt6.qtshadertools
    qt6.qtsvg
    qt6.qttools
    qt6.qtwayland
  ];

  cmakeFlags = [
    "-GNinja"
    (lib.cmakeBool "BUILD_TESTING" true)
    (lib.cmakeBool "BLOOM_BUNDLE_LIBMPV" false)
    (lib.cmakeBool "BLOOM_BUILD_VISUAL_TESTS" false)
  ];
  enableParallelBuilding = false;
  buildPhase = ''
    runHook preBuild
    cmake --build . --parallel "$NIX_BUILD_CORES" --target \
      BaseViewModelTest \
      LibraryCacheStoreTest \
      LibraryItemQueryTest \
      LibraryViewModelCanonicalTest \
      LoggingConfigTest \
      ConfigManagerThemeTest \
      ConnectionPersistenceTest \
      BloomProfileRepositoryTest \
      ProviderTransportTest \
      SiloAuthenticationTest \
      ProviderCatalogTest \
      SiloCatalogServiceTest \
      ArtworkRefreshTest \
      ImageCacheStoreTest \
      RoundedImageTest \
      SiloPlaybackProviderTest \
      CanonicalModelsTest \
      InputBindingManagerTest \
      PlayerBackendFactoryTest \
      PlayerProcessManagerTest \
      PlaybackPolicyTest \
      DisplayManagerTest \
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
    python3 "$src/tests/contracts/provider_contracts_test.py"
    export QT_QPA_PLATFORM=offscreen
    export QT_PLUGIN_PATH="${
      lib.makeSearchPath "lib/qt-6/plugins" [
        qt6.qtbase
        qt6.qtimageformats
      ]
    }''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
    ctest --output-on-failure \
      --exclude-regex '^(VisualRegressionTest|SeriesDetailsCacheTest)$'
    runHook postCheck
  '';
  installPhase = ''
    touch "$out"
  '';
}
