{
  lib,
  stdenv,
  cmake,
  makeWrapper,
  ninja,
  pkg-config,
  libsecret,
  mpv,
  SDL2,
  sqlite,
  qt6,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "bloom";
  version = lib.strings.removeSuffix "\n" (builtins.readFile ../VERSION);
  src = lib.cleanSource ../.;

  strictDeps = true;

  nativeBuildInputs = [
    cmake
    makeWrapper
    ninja
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    libsecret
    mpv
    SDL2
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
    (lib.cmakeBool "BUILD_TESTING" false)
    (lib.cmakeBool "BLOOM_BUNDLE_LIBMPV" false)
    (lib.cmakeFeature "BLOOM_BUILD_CHANNEL" "stable")
    (lib.cmakeFeature "BLOOM_BUILD_ID" finalAttrs.version)
  ];

  enableParallelBuilding = false;
  buildPhase = ''
    runHook preBuild
    cmake --build . --parallel "''${BLOOM_BUILD_JOBS:-2}"
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    cmake --install .
    runHook postInstall
  '';

  postInstall = ''
    if [ -e "$out/bin/Bloom" ]; then
      mv "$out/bin/Bloom" "$out/bin/bloom"
    fi
    substituteInPlace "$out/share/applications/com.github.crowquillx.Bloom.desktop" \
      --replace-fail "Exec=bloom" "Exec=$out/bin/bloom"
  '';

  qtWrapperArgs = [
    "--set-default"
    "QML_DISABLE_DISK_CACHE"
    "1"
    "--set-default"
    "QT_MEDIA_BACKEND"
    "ffmpeg"
    "--prefix"
    "PATH"
    ":"
    (lib.makeBinPath [ mpv ])
  ];

  meta = {
    description = "Jellyfin HTPC client built with Qt 6/QML and mpv";
    homepage = "https://github.com/crowquillx/Bloom";
    license = lib.licenses.mit;
    mainProgram = "bloom";
    platforms = [ "x86_64-linux" ];
  };
})
