#!/bin/sh
# Extract demo: run from the tools directory
# Usage: ./extract.sh [image.cdi]
cd "$(dirname "$0")"
if [ -z "$1" ]; then
    ./cdiextractor test.cdi -x -b -C test/
else
    ./cdiextractor "$1" -x -b -C test/
fi
