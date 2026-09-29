#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Use the host toolchain from Flatpak IDE terminals, as bw.sh does.
if [ -f /.flatpak-info ]; then
    if [ "${BUILD_DIR+x}" = x ]; then
        exec flatpak-spawn --host env BUILD_DIR="$BUILD_DIR" sh "$PWD/b.sh" "$@"
    fi
    exec flatpak-spawn --host sh "$PWD/b.sh" "$@"
fi
build_dir=${BUILD_DIR:-build-host}
for tool in cmake make ctest; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool" >&2
        echo "On Debian/Ubuntu: sudo apt install cmake build-essential libwxgtk3.2-dev" >&2
        exit 1
    fi
done
cmake -S . -B "$build_dir" -G "Unix Makefiles" "$@"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
# Preserve the original launch path without sharing CMake caches.
output_dir=$(cd "$build_dir" && pwd -P)
mkdir -p build
legacy_dir=$(cd build && pwd -P)
if [ "$output_dir" != "$legacy_dir" ]; then
    ln -sfn "$output_dir/searchwx" build/searchwx
fi
printf '\nLinux executable: %s/searchwx\nLaunch shortcut: %s/build/searchwx\n' "$output_dir" "$PWD"
