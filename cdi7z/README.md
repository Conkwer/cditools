# CDI 7-Zip Plugin

Open-source 7-Zip format plugin for DiscJuggler `.cdi` disc images.
Replacement for the closed-source Iso7z plugin, with IP.BIN boot sector display.

## Features

- Opens `.cdi` files (v2, v3, v3.5)
- Lists tracks with metadata: session, mode, sector size, LBA, pregap
- Extracts tracks as stripped `.iso` (2048-byte sectors) or raw `.wav` (2352-byte audio)
- Displays IP.BIN boot sector: Hardware ID, Maker, Region, Boot file, Version, Date
- Supports audio/data and data/data disc layouts
- Data tracks browsable as nested ISO9660 filesystems via `IInArchiveGetStream`

## Build

```bash
# Requires 7-Zip-zstd SDK: https://github.com/mcmilk/7-Zip-zstd
# Place this directory under: 7-Zip-zstd/CPP/7zip/Archive/Cdi/

# Linux
cd 7-Zip-zstd/CPP/7zip/Archive/Cdi
./build.sh

# Windows (cross-compile)
./build.sh win
```

For the full `7z.so` with CDI built-in, use the `build/` directory makefiles
with the 7-Zip-zstd source tree at `CPP/7zip/Bundles/Format7zF/`.

## Install

### Standalone plugin
Drop `CDI.so` (Linux) or `CDI.dll` (Windows) into your 7-Zip `Formats/` directory.

### Full replacement
Replace your `7z.so` with `bin/7z.so` and `7z` with `bin/7z`.

## Usage

```bash
# List tracks
7z l image.cdi

# Extract tracks (outputs .iso / .wav)
7z x image.cdi

# Extract files from the ISO (second pass)
7z x track.iso
```

## Files

| File | Description |
|------|-------------|
| `CdiHandler.h` | Archive handler class (IInArchive + IInArchiveGetStream) |
| `CdiHandler.cpp` | CDI parser, track extractor, IP.BIN reader |
| `CdiRegister.cpp` | Format registration (.cdi extension, class ID) |
| `build.sh` | Build script for Linux and Windows cross-compile |
| `build/` | Makefiles for full 7z.so build |
| `bin/CDI.so` | Standalone Linux format plugin |
| `bin/7z.so` | Full 7-zip library with CDI built-in |
| `bin/7z` | Linux loader binary |
