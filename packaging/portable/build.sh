#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake curl file fuse3 git libfuse2 \
    libdbus-1-3 libfontconfig1 libfreetype6 libgl1-mesa-dev libglib2.0-0t64 \
    libgthread-2.0-0 libice6 libmpv-dev libsecret-1-dev libsdl2-dev libsm6 \
    libsqlite3-dev libwayland-client0 libwayland-cursor0 libwayland-dev \
    libwayland-egl1 libx11-6 libx11-xcb1 libxdamage1 libxext6 libxfixes3 \
    libxft2 libxi6 libxkbcommon-dev libxkbcommon-x11-0 libxrandr2 libxrender1 \
    libxtst6 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render-util0 libxcb-render0 libxcb-shape0 libxcb-shm0 \
    libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libxcb1 mpv \
    ninja-build patchelf pkg-config python3-pip python3-venv zlib1g zsync

DEPS=/workspace/packaging/dependencies.json
jq_read() { python3 -c "import json; print(json.load(open('$DEPS'))$1)"; }
QT_VERSION="$(jq_read "['qt']['version']")"
LINUXDEPLOY_VERSION="$(jq_read "['portable']['linuxdeploy_version']")"
LINUXDEPLOY_SHA="$(jq_read "['portable']['linuxdeploy_sha256']")"
QT_PLUGIN_VERSION="$(jq_read "['portable']['linuxdeploy_qt_version']")"
QT_PLUGIN_SHA="$(jq_read "['portable']['linuxdeploy_qt_sha256']")"

python3 -m venv /opt/aqt
/opt/aqt/bin/pip install --no-cache-dir 'aqtinstall==3.3.*'
/opt/aqt/bin/aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
    -O /opt/Qt -m qtmultimedia qtshadertools qt5compat \
    qtwaylandcompositor

QT_ROOT="/opt/Qt/$QT_VERSION/gcc_64"
export PATH="$QT_ROOT/bin:$PATH"
export CMAKE_PREFIX_PATH="$QT_ROOT"
export LD_LIBRARY_PATH="$QT_ROOT/lib"
export APPIMAGE_EXTRACT_AND_RUN=1

curl -fsSL \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-x86_64.AppImage" \
    -o /usr/local/bin/linuxdeploy
echo "$LINUXDEPLOY_SHA  /usr/local/bin/linuxdeploy" | sha256sum -c -
chmod +x /usr/local/bin/linuxdeploy

curl -fsSL \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$QT_PLUGIN_VERSION/linuxdeploy-plugin-qt-x86_64.AppImage" \
    -o /usr/local/bin/linuxdeploy-plugin-qt
echo "$QT_PLUGIN_SHA  /usr/local/bin/linuxdeploy-plugin-qt" | sha256sum -c -
chmod +x /usr/local/bin/linuxdeploy-plugin-qt

cp -a /workspace /build
chmod -R u+w /build
cmake -S /build -B /build/out -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DBLOOM_BUILD_CHANNEL="$BLOOM_BUILD_CHANNEL" \
    -DBLOOM_BUILD_ID="$BLOOM_BUILD_ID" \
    -DBLOOM_GIT_SHA="$BLOOM_GIT_SHA"
cmake --build /build/out --parallel
DESTDIR=/build/stage cmake --install /build/out

mv /build/stage/usr/bin/Bloom /build/stage/usr/bin/bloom
mkdir -p /build/stage/usr/lib/bloom

# Bloom only uses QSQLITE. The online Qt SDK also ships drivers for external
# databases whose proprietary/client libraries are not part of the SDK.
find "$QT_ROOT/plugins/sqldrivers" -type f ! -name 'libqsqlite.so' -delete

APPDIR=/build/AppDir
QML_SOURCES_PATHS=/build/src/ui \
QML_MODULES_PATHS="$QT_ROOT/qml" \
EXTRA_QT_MODULES="waylandcompositor" \
EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland.so" \
linuxdeploy \
    --appdir "$APPDIR" \
    --executable /build/stage/usr/bin/bloom \
    --executable /usr/bin/mpv \
    --desktop-file /build/stage/usr/share/applications/com.github.crowquillx.Bloom.desktop \
    --icon-file /build/stage/usr/share/icons/hicolor/scalable/apps/com.github.crowquillx.Bloom.svg \
    --plugin qt

for platform_plugin in libqoffscreen.so libqwayland.so libqxcb.so; do
    test -f "$APPDIR/usr/plugins/platforms/$platform_plugin" || {
        echo "Missing deployed Qt platform plugin: $platform_plugin" >&2
        exit 1
    }
done

while IFS= read -r -d '' elf; do
    if unresolved="$(ldd "$elf" 2>/dev/null | grep 'not found' || true)" && [[ -n "$unresolved" ]]; then
        echo "Unresolved runtime dependencies in $elf:" >&2
        echo "$unresolved" >&2
        exit 1
    fi
done < <(find "$APPDIR/usr" -type f -print0)

QT_PLUGIN_PATH="$APPDIR/usr/plugins" \
QT_QPA_PLATFORM=offscreen \
"$APPDIR/usr/bin/bloom" --version
"$APPDIR/usr/bin/mpv" --version

cp -a /build/stage/usr/share/. "$APPDIR/usr/share/"
sed -i 's/^Exec=.*/Exec=bloom/' "$APPDIR/usr/share/applications/com.github.crowquillx.Bloom.desktop"

ARCH=x86_64 OUTPUT="/output/Bloom-${BLOOM_VERSION}-linux-x86_64.AppImage" \
    linuxdeploy --appdir "$APPDIR" --output appimage

APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen \
    "/output/Bloom-${BLOOM_VERSION}-linux-x86_64.AppImage" --version

tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
    -C "$APPDIR/usr" -czf "/output/Bloom-${BLOOM_VERSION}-linux-x86_64.tar.gz" .

DEBROOT=/build/deb
mkdir -p "$DEBROOT/DEBIAN" "$DEBROOT/usr"
cp -a "$APPDIR/usr/." "$DEBROOT/usr/"
cat > "$DEBROOT/DEBIAN/control" <<EOF
Package: bloom
Version: ${BLOOM_VERSION}
Section: video
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.39), libgl1, libsecret-1-0
Maintainer: Bloom Contributors <crowquillx@users.noreply.github.com>
Homepage: https://github.com/crowquillx/Bloom
Description: Jellyfin HTPC client with a 10-foot interface
EOF
dpkg-deb --build --root-owner-group "$DEBROOT" "/output/bloom_${BLOOM_VERSION}_amd64.deb"

cd /output
sha256sum \
    "Bloom-${BLOOM_VERSION}-linux-x86_64.AppImage" \
    "Bloom-${BLOOM_VERSION}-linux-x86_64.tar.gz" \
    "bloom_${BLOOM_VERSION}_amd64.deb" > SHA256SUMS-linux-x86_64.txt
