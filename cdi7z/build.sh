#!/bin/bash
# Build CDI handler objects for 7-Zip-zstd
# Usage: ./build.sh [win]
#   no args = Linux (.o for linking into 7z.so)
#   win     = Windows cross-compile (.o for linking into 7z.dll)

set -e
S="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="$S/7zip/Bundles/Format7zF/_o"

CXXFLAGS="-std=c++17 -O2 -fPIC -Wno-unused-function \
  -DZ7_EXTERNAL_CODECS -DNDEBUG -D_REENTRANT \
  -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I$S/7zip -I$S -I$S/myWindows -I$S/../../C"

mkdir -p "$OUT"

if [ "$1" = "win" ]; then
    CXX=x86_64-w64-mingw32-g++
    CXXFLAGS="$CXXFLAGS -D_WIN32 -DUNICODE -D_UNICODE"
else
    CXX=g++
fi

echo "Building CDI handler for ${1:-Linux}..."
$CXX $CXXFLAGS -c "$S/7zip/Archive/Cdi/CdiHandler.cpp" -o "$OUT/CdiHandler.o"
$CXX $CXXFLAGS -c "$S/7zip/Archive/Cdi/CdiRegister.cpp" -o "$OUT/CdiRegister.o"

echo "Done: $OUT/CdiHandler.o $OUT/CdiRegister.o"
