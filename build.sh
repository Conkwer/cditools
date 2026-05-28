#!/bin/sh
# Build all CDI tools (Linux)
# Requires: g++, genisoimage/mkisofs in PATH

set -e
echo "=== Building cdiextractor ==="
g++ -std=c++17 -O2 -s -o cdiextractor cdiextractor.cpp
echo "=== Building cdibuilder ==="
g++ -std=c++17 -O2 -s -o cdibuilder cdibuilder.cpp
echo "=== Done ==="
