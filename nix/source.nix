{
  lib,
  includeTests ? false,
}:

lib.fileset.toSource {
  root = ../.;
  fileset = lib.fileset.unions (
    [
      ../CMakeLists.txt
      ../VERSION
      ../cmake
      ../config
      ../packaging/dependencies.json
      ../src
      ../third_party/monocypher
    ]
    ++ lib.optionals includeTests [
      ../scripts/generate-update-manifest.py
      ../scripts/verify-dolby-vision-fel.py
      ../tests
    ]
  );
}
