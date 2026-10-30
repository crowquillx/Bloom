# Maintainer: crowquillx
#
# Development package built from the main branch. pkgver() derives the version
# from the VERSION file and the commit, so release bumps need no edit here.
pkgname=bloom-jellyfin-git
_srcname=Bloom
pkgver=0.8.0
pkgrel=1
pkgdesc="Media client for Jellyfin and Silo servers"
arch=('x86_64')
url="https://github.com/crowquillx/Bloom"
license=('MIT')
depends=(
    'qt6-base>=6.10'
    'qt6-declarative>=6.10'
    'qt6-multimedia>=6.10'
    'qt6-wayland>=6.10'
    'qt6-5compat>=6.10'
    'qt6-imageformats>=6.10'
    'qt6-svg>=6.10'
    'sqlite'
    'mpv'
    'libsecret'
    'sdl2'
)
makedepends=(
    'cmake'
    'ninja'
    'git'
    'pkgconf'
    'qt6-tools>=6.10'
    'qt6-shadertools>=6.10'
)
provides=('bloom-jellyfin')
conflicts=('bloom-jellyfin')
source=("$_srcname::git+https://github.com/crowquillx/Bloom.git#branch=main")
sha256sums=('SKIP')

pkgver() {
    cd "$_srcname"
    printf '%s.r%s.g%s' \
        "$(tr -d '\n' < VERSION)" \
        "$(git rev-list --count HEAD)" \
        "$(git rev-parse --short=7 HEAD)"
}

build() {
    # Arch provides mpv/libmpv; do not copy libmpv beside Bloom.
    cmake -S "$_srcname" -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF \
        -DBLOOM_BUNDLE_LIBMPV=OFF
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
    install -Dm644 "$_srcname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
