#!/usr/bin/env bash
# Builds the Debian package from the deployed portable AppDir.
#
# The AppDir bundles Qt, FFmpeg, libmpv, Wayland/Vulkan clients, a qt.conf, and
# its own mpv. All of it stays under /opt/bloom so it never replaces or shadows
# distribution packages such as mpv. /usr receives only the bloom launcher and
# the desktop entry, icon, and AppStream metainfo.
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 APPDIR VERSION OUTPUT_DEB" >&2
    exit 2
fi
APPDIR="$1"
VERSION="$2"
OUTPUT_DEB="$3"
APP_ID=com.github.crowquillx.Bloom
PREFIX=/opt/bloom

DEBROOT="$(mktemp -d)"
trap 'rm -rf "$DEBROOT"' EXIT
mkdir -p "$DEBROOT/DEBIAN" "$DEBROOT$PREFIX" "$DEBROOT/usr/bin" "$DEBROOT/usr/share"
# mktemp creates a 0700 directory; dpkg-deb records it as the package's "./".
chmod 0755 "$DEBROOT" "$DEBROOT/DEBIAN"
cp -a "$APPDIR/usr/." "$DEBROOT$PREFIX/"
for dir in applications icons metainfo; do
    mv "$DEBROOT$PREFIX/share/$dir" "$DEBROOT/usr/share/$dir"
done
# Qt's applicationDirPath() and the loader's $ORIGIN both resolve this symlink,
# so Bloom still finds its qt.conf, bundled libraries, plugins, QML, mpv, and
# seek-preview helper in $PREFIX/bin.
ln -s "$PREFIX/bin/bloom" "$DEBROOT/usr/bin/bloom"

unexpected="$(cd "$DEBROOT" && find usr \( -type f -o -type l \) \
    ! -path usr/bin/bloom ! -name "$APP_ID.*")"
if [[ -n "$unexpected" ]]; then
    echo "Debian package would install outside $PREFIX:" >&2
    echo "$unexpected" >&2
    exit 1
fi

cat > "$DEBROOT/DEBIAN/control" <<EOF
Package: bloom
Version: ${VERSION}
Section: video
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.39), libgl1, libopengl0, libsecret-1-0
Maintainer: Bloom Contributors <crowquillx@users.noreply.github.com>
Homepage: https://github.com/crowquillx/Bloom
Description: Media client for Jellyfin and Silo servers
EOF
dpkg-deb --build --root-owner-group "$DEBROOT" "$OUTPUT_DEB"
