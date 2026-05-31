# CDI 7-Zip Plugin

Open-source 7-Zip format plugin for DiscJuggler `.cdi` disc images.
Replacement for the closed-source Iso7z plugin, with IP.BIN boot sector display.

**Platform:** Linux x86_64 only (compiled as part of 7-Zip-zstd).
Windows users can use the existing [Iso7z](https://www.tc4shell.com/en/7zip/iso7z/)
plugin instead.

## Features

- Opens `.cdi` files (v2, v3, v3.5)
- **Single-step extraction** — `7z x image.cdi` extracts files directly
- Built-in ISO9660 filesystem walker — no intermediate track ISO step
- Displays IP.BIN boot sector: Hardware ID, Maker, Region, Boot file, Version, Date
- Supports audio/data and data/data disc layouts
- File dates and attributes from ISO9660 directory records

## Usage

```bash
# List files inside the CDI
LD_LIBRARY_PATH=. ./7z l image.cdi

# Extract all files to a subfolder
LD_LIBRARY_PATH=. ./7z x image.cdi -o./extracted

# Extract to an absolute path
LD_LIBRARY_PATH=. ./7z x image.cdi -o/tmp/output

# Test archive integrity
LD_LIBRARY_PATH=. ./7z t image.cdi

# Verbose detail listing
LD_LIBRARY_PATH=. ./7z l -slt image.cdi
```

**Note:** `-o` specifies the output directory with no space after it (e.g., `-o./out` not `-o ./out`).

## Install

Replace your system `7z.so` with `bin/7z.so`, or use the provided loader:

```bash
cd bin
LD_LIBRARY_PATH=. ./7z l /path/to/image.cdi
```

To install system-wide:
```bash
cp bin/7z.so /opt/7zip-zstd/7z.so   # (backup the original first)
```

## Build

```bash
# Requires 7-Zip-zstd SDK: https://github.com/mcmilk/7-Zip-zstd
# Place source files under: 7-Zip-zstd/CPP/7zip/Archive/Cdi/

# Linux
./build.sh

# For the full 7z.so with CDI built-in, use build/makefile.gcc and
# build/Arc_gcc.mak with the 7-Zip-zstd Format7zF bundle
```

## Files

| File | Description |
|------|-------------|
| `CdiHandler.h` | Archive handler class |
| `CdiHandler.cpp` | CDI parser + ISO9660 walker + extractor |
| `CdiRegister.cpp` | Format registration (.cdi extension) |
| `build.sh` | Build script (Linux, Windows cross-compile) |
| `build/` | Makefiles for full 7z.so rebuild |
| `bin/CDI.so` | Standalone Linux format plugin (135 KB) |
| `bin/7z.so` | Full 7-zip library with CDI built-in (5.6 MB) |
| `bin/7z` | Linux loader binary (667 KB) |
