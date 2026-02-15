# Maintainer: crowquillx
pkgname=bloom-jellyfin
pkgver=0.2.0
pkgrel=1
pkgdesc="Jellyfin HTPC client with 10-foot UI"
arch=('x86_64')
url="https://github.com/crowquillx/Bloom"
license=('MIT')
depends=(
    'qt6-base>=6.10'
    'qt6-declarative>=6.10'
    'qt6-tools>=6.10'
    'qt6-multimedia>=6.10'
    'qt6-wayland>=6.10'
    'qt6-5compat>=6.10'
    'qt6-shadertools>=6.10'
    'sqlite'
    'mpv'
    'libsecret'
)
makedepends=('cmake' 'ninja' 'git' 'pkgconf')
source=("${pkgname}-${pkgver}.tar.gz::https://github.com/crowquillx/Bloom/archive/refs/tags/dev-latest.tar.gz")
sha256sums=('SKIP')

build() {
    cd "Bloom-dev-latest"
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF
    cmake --build build
}

package() {
    cd "Bloom-dev-latest"
    DESTDIR="$pkgdir" cmake --install build
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
