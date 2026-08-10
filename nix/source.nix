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
      ../config
      ../src
    ]
    ++ lib.optionals includeTests [ ../tests ]
  );
}
