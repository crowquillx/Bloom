#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake curl file fuse3 git libfuse2 \
    libgl1-mesa-dev libmpv-dev libsecret-1-dev libsqlite3-dev libwayland-dev mpv \
    libxkbcommon-dev ninja-build patchelf pkg-config python3-pip python3-venv \
    zsync

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
    -O /opt/Qt -m qtmultimedia qtshadertools qtimageformats qt5compat

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

APPDIR=/build/AppDir
QML_SOURCES_PATHS=/build/src/ui \
QML_MODULES_PATHS="$QT_ROOT/qml" \
EXTRA_QT_PLUGINS="waylandcompositor;wayland-decoration-client;wayland-graphics-integration-client" \
linuxdeploy \
    --appdir "$APPDIR" \
    --executable /build/stage/usr/bin/bloom \
    --executable /usr/bin/mpv \
    --desktop-file /build/stage/usr/share/applications/com.github.crowquillx.Bloom.desktop \
    --icon-file /build/stage/usr/share/icons/hicolor/scalable/apps/com.github.crowquillx.Bloom.svg \
    --plugin qt

cp -a /build/stage/usr/share/. "$APPDIR/usr/share/"
sed -i 's/^Exec=.*/Exec=bloom/' "$APPDIR/usr/share/applications/com.github.crowquillx.Bloom.desktop"

ARCH=x86_64 OUTPUT="/output/Bloom-${BLOOM_VERSION}-linux-x86_64.AppImage" \
    linuxdeploy --appdir "$APPDIR" --output appimage

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
