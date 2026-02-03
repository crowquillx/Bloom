# Maintainer: Your Name <your@email.com>
pkgname=bloom-git
pkgver=0.2.0
pkgrel=1
pkgdesc="Jellyfin HTPC client with 10-foot UI"
arch=('x86_64')
url="https://github.com/yourusername/Bloom"
license=('custom')
depends=(
    'qt6-base>=6.10'
    'qt6-declarative>=6.10'
    'qt6-tools>=6.10'
    'qt6-multimedia>=6.10'
    'qt6-wayland>=6.10'
    'qt6-5compat>=6.10'
    'sqlite'
    'mpv'
)
makedepends=('cmake' 'ninja' 'git')
source=("git+file://$(pwd)")
sha256sums=('SKIP')

build() {
    cd "$srcdir/Bloom"
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

package() {
    cd "$srcdir/Bloom"
    DESTDIR="$pkgdir" cmake --install build
    
    # Install desktop file if you create one
    # install -Dm644 bloom.desktop "$pkgdir/usr/share/applications/bloom.desktop"
}
