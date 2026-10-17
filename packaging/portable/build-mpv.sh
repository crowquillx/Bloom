#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS="$ROOT/packaging/dependencies.json"
PREFIX=/opt/bloom-mpv
SOURCES=/tmp/bloom-mpv-sources
mkdir -p "$SOURCES" "$PREFIX"
pin() { python3 -c "import json; print(json.load(open('$DEPS'))$1)"; }

apt-get install -y --no-install-recommends \
    autoconf automake libtool meson nasm python3-jinja2 \
    libass-dev libasound2-dev libdav1d-dev libdrm-dev libegl1-mesa-dev \
    libepoxy-dev libexpat1-dev libffi-dev libgbm-dev glslang-dev \
    libgnutls28-dev libjpeg-dev liblcms2-dev libpulse-dev libva-dev \
    libvdpau-dev libxinerama-dev libxml2-dev libxpresent-dev libxrandr-dev libxss-dev

export PATH="$PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

fetch_source() {
    local name="$1" url="$2" hash="$3"
    curl -fsSL --retry 3 "$url" -o "$SOURCES/$name.tar"
    echo "$hash  $SOURCES/$name.tar" | sha256sum -c -
    mkdir -p "$SOURCES/$name"
    tar --no-same-owner -xf "$SOURCES/$name.tar" -C "$SOURCES/$name" --strip-components=1
}

meson_install() {
    local name="$1"
    shift
    meson setup "$SOURCES/$name/out" "$SOURCES/$name" \
        --prefix="$PREFIX" --libdir=lib --buildtype=release --wrap-mode=nodownload "$@"
    meson compile -C "$SOURCES/$name/out" -j "${BLOOM_BUILD_JOBS:-4}"
    meson install -C "$SOURCES/$name/out"
}

wayland_version="$(pin "['portable']['wayland_version']")"
fetch_source wayland \
    "https://gitlab.freedesktop.org/wayland/wayland/-/releases/$wayland_version/downloads/wayland-$wayland_version.tar.xz" \
    "$(pin "['portable']['wayland_sha256']")"
meson_install wayland -Ddocumentation=false -Dtests=false
protocols_version="$(pin "['portable']['wayland_protocols_version']")"
fetch_source wayland-protocols \
    "https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/$protocols_version/downloads/wayland-protocols-$protocols_version.tar.xz" \
    "$(pin "['portable']['wayland_protocols_sha256']")"
meson_install wayland-protocols -Dtests=false

vulkan_tag="$(pin "['portable']['vulkan_tag']")"
for project in Headers Loader; do
    name="vulkan-${project,,}"
    fetch_source "$name" "https://github.com/KhronosGroup/Vulkan-$project/archive/refs/tags/$vulkan_tag.tar.gz" \
        "$(pin "['portable']['${name//-/_}_sha256']")"
    cmake -S "$SOURCES/$name" -B "$SOURCES/$name/out" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DBUILD_TESTS=OFF
    cmake --build "$SOURCES/$name/out" --parallel "${BLOOM_BUILD_JOBS:-4}"
    cmake --install "$SOURCES/$name/out"
done

ffmpeg_version="$(pin "['mpv']['linux_ffmpeg_version']")"
fetch_source ffmpeg "https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz" \
    "$(pin "['mpv']['linux_ffmpeg_sha256']")"
nvcodec_version="$(pin "['portable']['nvcodec_version']")"
fetch_source nv-codec-headers \
    "https://github.com/FFmpeg/nv-codec-headers/releases/download/n$nvcodec_version/nv-codec-headers-$nvcodec_version.tar.gz" \
    "$(pin "['portable']['nvcodec_sha256']")"
make -C "$SOURCES/nv-codec-headers" install PREFIX="$PREFIX"
(
    cd "$SOURCES/ffmpeg"
    ./configure --prefix="$PREFIX" --enable-shared --disable-static \
        --disable-programs --disable-doc --disable-debug --enable-gnutls \
        --enable-libdav1d --enable-libdrm --enable-vaapi --enable-ffnvcodec \
        --enable-nvdec --disable-vulkan
    make -j "${BLOOM_BUILD_JOBS:-4}"
    make install
)

placebo_revision="$(pin "['mpv']['libplacebo_revision']")"
git init "$SOURCES/libplacebo"
git -C "$SOURCES/libplacebo" remote add origin https://github.com/haasn/libplacebo.git
git -C "$SOURCES/libplacebo" fetch --depth=1 origin "$placebo_revision"
git -C "$SOURCES/libplacebo" checkout --detach FETCH_HEAD
git -C "$SOURCES/libplacebo" submodule update --init --recursive --depth=1
meson_install libplacebo -Ddemos=false -Dshaderc=disabled -Dglslang=enabled -Dvulkan=enabled

mpv_revision="$(pin "['mpv']['source_revision']")"
fetch_source mpv "https://github.com/mpv-player/mpv/archive/$mpv_revision.tar.gz" \
    "$(pin "['mpv']['source_sha256']")"
pin "['mpv']['describe']" | sed 's/^v//' > "$SOURCES/mpv/MPV_VERSION"
meson_install mpv -Dlibmpv=true -Dcplayer=true -Dbuild-date=false \
    -Dlua=disabled -Djavascript=disabled -Dlibarchive=disabled -Duchardet=disabled \
    -Dshaderc=disabled -Dvulkan=enabled -Dwayland=enabled -Dmanpage-build=disabled -Dtests=false

for project in wayland wayland-protocols vulkan-loader vulkan-headers ffmpeg libplacebo mpv; do
    mkdir -p "$PREFIX/share/licenses/$project"
    for license in "$SOURCES/$project"/COPYING* "$SOURCES/$project"/LICENSE* "$SOURCES/$project"/Copyright; do
        if [[ -f "$license" ]]; then
            cp "$license" "$PREFIX/share/licenses/$project/"
        elif [[ -d "$license" ]]; then
            cp -a "$license" "$PREFIX/share/licenses/$project/"
        fi
    done
done

python3 -I "$ROOT/scripts/check-mpv-fel-capability.py" --dependencies "$DEPS" \
    --mpv "$PREFIX/bin/mpv" --library "$PREFIX/lib/libmpv.so" \
    --library "$PREFIX/lib/libavcodec.so" --library "$PREFIX/lib/libplacebo.so" \
    --require-fel --require-pin
