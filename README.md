# cditools

Fast command-line tools for unpacking and building DiscJuggler (.cdi) Dreamcast images.

## Tools

| Tool | Purpose |
|------|---------|
| `cdiextractor` | Extract files, boot sector, and CRC32 scan lists from CDI images |
| `cdibuilder` | Build CDI images from directories (audio/data and data/data) |

## Quick Start (Windows)

Drop a `.cdi` onto `extract.cmd` — extracts all files + bootsector + Dreambeam scan list into `data/`.

Or from command line:

```
cdiextractor game.cdi
cdibuilder -d ./game_files -V "MYGAME" -o game.cdi
```

## Building from source

```
g++ -std=c++17 -O2 -o cdiextractor cdiextractor.cpp
g++ -std=c++17 -O2 -o cdibuilder cdibuilder.cpp
```

Windows: use `build.cmd` (requires MinGW-w64).

`cdibuilder` also requires `mkisofs` or `genisoimage` in PATH.

## cdiextractor — Extract & scan

```
cdiextractor <image.cdi | directory> [options]

  -l, --list        List files inside the CDI
  -x, --extract     Extract files
  -b, --bootsector  Also extract IP.BIN boot sector as bootsector.bin
  -s, --scan        Generate Dreambeam-compatible _dbscan.txt (CRC32 hashes)
  -m, --match <db>  Match scan against Dreambeam database
  -C <dir>          Output directory (default: data/)
  -a, --all         Include directories when listing
```

## cdibuilder — Build CDI

```
cdibuilder -d <input_dir> [options]

  -d <dir>      Input directory with game files + IP.BIN (runs mkisofs)
  -I <iso>      Input ISO file (skips mkisofs)
  -o <cdi>      Output CDI file (default: <volume>.cdi)
  -V <name>     Volume name (default: dcgame)
  -l <lba>      Session 2 LBA: 11702 for audio/data, 45000 for data/data
  -t <type>     Image type: audio or data (default: audio)
```

## Limitations

- `cdibuilder` audio/data mode only (data/data coming)
- No ECC/EDC generation (emulators and ODEs don't require it)
- No CDDA audio track support
