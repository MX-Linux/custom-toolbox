#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
repo=$PWD
app=custom-toolbox
output=$repo/build-appimage
tools=$output/tools

if [[ $(uname -sm) != 'Linux x86_64' ]]; then
    echo "This script builds an x86-64 AppImage on x86-64 Linux." >&2
    exit 1
fi

mkdir -p "$tools"
work=$(mktemp -d "$output/staging.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

# Keep verified tools for subsequent offline builds.
fetch() {
    local name=$1 url=$2 digest=$3
    if [[ ! -f $tools/$name ]]; then
        curl --fail --location --retry 3 --output "$work/$name" "$url"
        (cd "$work" && echo "$digest  $name" | sha256sum --check)
        mv -f -- "$work/$name" "$tools/$name"
    else
        (cd "$tools" && echo "$digest  $name" | sha256sum --check)
    fi
}

deploy=linuxdeploy-x86_64.AppImage
fetch "$deploy" "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/$deploy" \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
fetch runtime-x86_64 https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
    2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
chmod 755 "$tools/$deploy" "$tools/runtime-x86_64"

build=$work/build
appdir=$work/AppDir
cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DAPPIMAGE_BUILD=ON -DBUILD_TESTING=OFF
cmake --build "$build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
DESTDIR="$appdir" cmake --install "$build"

prefix=$appdir/usr
install -Dm644 LICENSE "$prefix/share/doc/custom-toolbox/LICENSE"
install -Dm644 "icons/custom-toolbox.svg" "$prefix/share/icons/hicolor/scalable/apps/custom-toolbox.svg"

qt_plugins=$(qmake6 -query QT_INSTALL_PLUGINS)
plugins=$prefix/plugins
install -Dm644 -t "$plugins/platforms" "$qt_plugins/platforms/libqxcb.so" "$qt_plugins"/platforms/libqwayland*.so \
    "$qt_plugins/platforms/libqoffscreen.so"
deploy_deps=(--deploy-deps-only "$plugins/platforms")
install -Dm644 -t "$plugins/imageformats" "$qt_plugins"/imageformats/libq{gif,ico,jpeg,svg}.so
deploy_deps+=(--deploy-deps-only "$plugins/imageformats")
for group in iconengines xcbglintegrations platforminputcontexts \
             wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    install -Dm644 -t "$plugins/$group" "$qt_plugins/$group/"*.so
    deploy_deps+=(--deploy-deps-only "$plugins/$group")
done
translations=$(qmake6 -query QT_INSTALL_TRANSLATIONS)
install -Dm644 -t "$prefix/translations" "$translations"/qt{,base}_*.qm
cat > "$prefix/bin/qt.conf" <<'CONF'
[Paths]
Prefix=..
Plugins=plugins
Translations=translations
QmlImports=qml
CONF

qml=$(qmake6 -query QT_INSTALL_QML)
mkdir -p "$prefix/qml/QtQuick"
cp -R "$qml"/{QtCore,QtQml} "$prefix/qml/"
install -m644 "$qml"/QtQuick/{qmldir,libqtquick2plugin.so} "$prefix/qml/QtQuick/"
cp -R "$qml"/QtQuick/{Controls,Layouts,NativeStyle,Shapes,Templates,Window} "$prefix/qml/QtQuick/"
while IFS= read -r -d '' plugin; do
    deploy_deps+=(--deploy-deps-only "$plugin")
done < <(find "$prefix/qml" -name '*.so' -print0)

cat > "$work/AppRun" <<'RUN'
#!/bin/sh
set -eu
prefix=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)/usr
export XDG_DATA_DIRS="$prefix/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "$prefix/bin/custom-toolbox" "$@"
RUN
chmod 755 "$work/AppRun"

version=$(dpkg-parsechangelog -SVersion)
filename=$app-$version-x86_64.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_RUNTIME_FILE="$tools/runtime-x86_64"
export LDAI_OUTPUT="$work/$filename"
"$tools/$deploy" --appdir "$appdir" "${deploy_deps[@]}" --custom-apprun "$work/AppRun" \
    --desktop-file "$app.desktop" --icon-file "icons/custom-toolbox.svg" --output appimage

if [[ $(env -u DISPLAY -u WAYLAND_DISPLAY "$work/$filename" --version) != "$app $version" ]]; then
    echo "The packaged application failed its version check." >&2
    exit 1
fi
mv -f -- "$work/$filename" "$output/$filename"
echo "Built $output/$filename"
