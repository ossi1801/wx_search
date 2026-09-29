#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Keep SDK and host caches separate: their /usr and /tmp paths differ.
if [ -f /.flatpak-info ]; then
    default_build_dir=build-flatpak
else
    default_build_dir=build-host
fi
build_dir=${BUILD_DIR:-$default_build_dir}
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
