Third-Party Notices

This project includes and/or distributes third-party software.

Bloom project license
- Bloom source code is licensed under the MIT License.
- See `LICENSE`.

libmpv / mpv
- Bloom uses mpv/libmpv for playback.
- Windows distribution artifacts may bundle a `libmpv` runtime DLL.
- CI currently sources Windows libmpv SDK/runtime from:
  - https://github.com/shinchiro/mpv-winbuild-cmake
- Upstream mpv project:
  - https://mpv.io/
  - https://github.com/mpv-player/mpv

Licensing for bundled libmpv
- Bloom CI is configured to use `mpv-dev` SDK artifacts from the source above.
- mpv/libmpv licensing details are defined by the upstream artifact and its included license files.
- When redistributing Bloom binaries that bundle libmpv, include the corresponding upstream license texts/notices provided with that libmpv artifact.

Important note
- If a non-LGPL mpv/libmpv artifact is used for packaging, licensing obligations may differ.
- Verify the exact license metadata of the bundled mpv/libmpv package before distribution.

Attribution
- Bloom's embedded mpv architecture was inspired by Plezy:
  - https://github.com/edde746/plezy
