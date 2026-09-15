# Maintainer: Adrian <adrian@mxlinux.org>
pkgname=custom-toolbox
pkgver=${PKGVER:-26.09.5}
pkgrel=1
pkgdesc="Graphical launcher toolbox for user-defined .list files"
arch=('x86_64' 'i686')
url="https://mxlinux.org"
license=('GPL3')
depends=('qt6-base' 'qt6-declarative' 'polkit')
makedepends=('cmake' 'ninja' 'qt6-declarative' 'qt6-tools')
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
    install -dm755 "${pkgdir}/usr/share/man/man1"

    install -m644 help/*.1 "${pkgdir}/usr/share/man/man1/"
    if [ -d help ]; then
        for help_file in help/*.html help/*.jpg help/*.png help/*.css; do
            [ -f "$help_file" ] && install -Dm644 "$help_file" "${pkgdir}/usr/share/doc/custom-toolbox/$(basename "$help_file")"
        done
    fi

    # Install changelog
    gzip -c debian/changelog > "${pkgdir}/usr/share/doc/custom-toolbox/changelog.gz"
}
