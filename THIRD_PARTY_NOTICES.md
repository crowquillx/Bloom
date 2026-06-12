Third-Party Notices

This project includes and/or distributes third-party software.

Bloom project license
- Bloom source code is licensed under the MIT License.
- See `LICENSE`.

libmpv / mpv
- Bloom uses mpv/libmpv for playback.
- Windows distribution artifacts may bundle a `libmpv` runtime DLL.
- CI currently sources Windows libmpv SDK/runtime from:
  - https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20260610/mpv-dev-x86_64-20260610-git-304426c.7z
- Upstream mpv project:
  - https://mpv.io/
  - https://github.com/mpv-player/mpv

Licensing for bundled libmpv
- Bloom CI and the Windows auto-fetch helper are configured to use the pinned `mpv-dev-x86_64-20260610-git-304426c.7z` SDK artifact from the source above.
- mpv/libmpv licensing details are defined by the upstream artifact and its included license files.
- When redistributing Bloom binaries that bundle libmpv, include the corresponding upstream license texts/notices provided with that libmpv artifact.

Important note
- If a non-LGPL mpv/libmpv artifact is used for packaging, licensing obligations may differ.
- Verify the exact license metadata of the bundled mpv/libmpv package before distribution.

Bundled mpv GLSL shaders
- Bloom bundles these shader files under `src/resources/mpv-shaders/` and copies them to Bloom's mpv config shader directory at runtime:
  - `FSRCNNX_x2_8-0-4-1.glsl`
  - `KrigBilateral.glsl`
  - `SSimDownscaler.glsl`
  - `ArtCNN_C4F16.glsl`
  - `nnedi3-nns32-win8x6.hook`
- `FSRCNNX_x2_8-0-4-1.glsl`, `KrigBilateral.glsl`, `SSimDownscaler.glsl`, and
  `nnedi3-nns32-win8x6.hook` state GNU Lesser General Public License version 3.0 or later.
- `ArtCNN_C4F16.glsl` states the MIT License in its file header.
- Sources:
  - FSRCNNX: https://github.com/igv/FSRCNN-TensorFlow/releases/tag/1.1
  - KrigBilateral: https://gist.github.com/igv/a015fc885d5c22e6891820ad89555637
  - SSimDownscaler: https://gist.github.com/igv/36508af3ffc84410fe39761d6969be10
  - ArtCNN: https://github.com/Artoriuz/ArtCNN
  - nnedi3: https://github.com/bjin/mpv-prescalers

Bundled Gandhi Sans fonts
- Bloom bundles Gandhi Sans OpenType fonts under `src/resources/mpv-fonts/` and copies them
  to Bloom's mpv config font directory at runtime for libass subtitle styling.
- License text is included at `src/resources/mpv-fonts/LICENSE-GandhiSans.txt`.
- Source/license reference: https://www.fontsquirrel.com/license/gandhi-sans

Attribution
- Bloom's embedded mpv architecture was inspired by Plezy:
  - https://github.com/edde746/plezy
