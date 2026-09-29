#!/bin/sh
# Package existing Linux and Windows builds; pass options through to the packager.
set -eu
cd "$(dirname "$0")"
exec python3 ./package_release.py "$@"
