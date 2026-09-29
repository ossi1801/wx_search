#!/bin/sh
# Build a 64-bit Windows executable on Linux, including a local static wxWidgets SDK.
set -eu
cd "$(dirname "$0")"
# The host owns the Debian MinGW installation, not the IDE's Flatpak runtime.
if [ -f /.flatpak-info ]; then
    exec flatpak-spawn --host sh "$PWD/bw.sh" "$@"
fi
: "${CXX:=x86_64-w64-mingw32-g++-posix}"
: "${CC:=x86_64-w64-mingw32-gcc-posix}"
: "${JOBS:=4}"
for tool in "$CXX" "$CC" make curl tar sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing Windows build tool: $tool" >&2
        echo "On Debian/Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix make curl bzip2" >&2
        exit 1
    fi
done
output="$PWD/build_windows"
sdk="$output/wx-sdk"
mkdir -p "$output"
if [ -z "${WX_CONFIG:-}" ]; then
    WX_CONFIG="$sdk/bin/wx-config"
    if [ ! -x "$WX_CONFIG" ]; then
        archive="$output/wxWidgets-3.2.8.tar.bz2"
        source_dir="$output/wx-source"
        if [ ! -f "$archive" ]; then
            curl -fL --retry 3 https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.8/wxWidgets-3.2.8.tar.bz2 -o "$archive.part"
            mv "$archive.part" "$archive"
        fi
        printf '%s  %s\n' c74784904109d7229e6894c85cfa068f1106a4a07c144afd78af41f373ee0fe6 "$archive" | sha256sum -c -
        if [ ! -f "$source_dir/configure" ]; then
            mkdir -p "$source_dir"
            tar -xf "$archive" -C "$source_dir" --strip-components=1
        fi
        mkdir -p "$output/wx-build"
        (
            cd "$output/wx-build"
            CC="$CC" CXX="$CXX" "$source_dir/configure" \
                --host=x86_64-w64-mingw32 --prefix="$sdk" --with-msw \
                --disable-shared --disable-debug --disable-precomp-headers \
                --disable-webview --disable-mediactrl --disable-richtext \
                --disable-stc --disable-ribbon --disable-propgrid --disable-aui \
                --without-opengl --without-libtiff --without-libjpeg
            make -j "$JOBS"
            make install
        )
    fi
fi
# wx-config deliberately emits a resource command and separate compiler/linker arguments.
$("$WX_CONFIG" --rescomp) -i resources/windows.rc -o "$output/windows-resources.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra main.cpp $("$WX_CONFIG" --cxxflags --libs core,base) \
    "$output/windows-resources.o" -o "$output/search.exe" -static -static-libgcc -static-libstdc++ -mwindows
printf '\nWindows executable: %s/search.exe\n' "$output"
