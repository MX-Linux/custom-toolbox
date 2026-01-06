# Maintainer: Adrian <adrian@mxlinux.org>
pkgname=custom-toolbox
pkgver=${PKGVER:-25.11}
pkgrel=1
pkgdesc="Graphical launcher toolbox for user-defined .list files"
arch=('x86_64' 'i686')
url="https://mxlinux.org"
license=('GPL3')
depends=('qt6-base' 'polkit')
makedepends=('cmake' 'ninja' 'qt6-tools')
source=()
sha256sums=()

build() {
    cd "${startdir}"

    # Clean any previous build artifacts
    rm -rf build

    cmake -G Ninja \
        -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DPROJECT_VERSION_OVERRIDE="${pkgver}"

    cmake --build build --parallel
}

package() {
    cd "${startdir}"

    # Install binary
    install -Dm755 build/custom-toolbox "${pkgdir}/usr/bin/custom-toolbox"

    # Install translations
    install -dm755 "${pkgdir}/usr/share/custom-toolbox/locale"
    install -Dm644 build/*.qm "${pkgdir}/usr/share/custom-toolbox/locale/" 2>/dev/null || true

    # Install configuration samples
    install -dm755 "${pkgdir}/etc/custom-toolbox"
    install -Dm644 custom-toolbox.conf "${pkgdir}/etc/custom-toolbox/custom-toolbox.conf"
    install -Dm644 example.list "${pkgdir}/etc/custom-toolbox/example.list"

    # Install desktop entry
    install -Dm644 custom-toolbox.desktop "${pkgdir}/usr/share/applications/custom-toolbox.desktop"

    # Install icons
    install -Dm644 icons/custom-toolbox.svg "${pkgdir}/usr/share/icons/hicolor/scalable/apps/custom-toolbox.svg"
    install -Dm644 icons/custom-toolbox.svg "${pkgdir}/usr/share/pixmaps/custom-toolbox.svg"

    # Install documentation
    install -dm755 "${pkgdir}/usr/share/doc/custom-toolbox"
    if [ -d help ]; then
        cp -r help/* "${pkgdir}/usr/share/doc/custom-toolbox/" 2>/dev/null || true
    fi

    # Install changelog
    gzip -c debian/changelog > "${pkgdir}/usr/share/doc/custom-toolbox/changelog.gz"
}
