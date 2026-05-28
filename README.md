# cditools

Fast C++ command-line tools for unpacking and building DiscJuggler (.cdi)
Sega Dreamcast images. Designed for automated systems and AI agent toolchains
in Linux (Debian 12 tested) and Windows (WinXP or newer, 64-bit) environments.

The purpose is to replace the Python-dependent lazyboot toolchain, img4dc,
mkcdi, mkdcdisc, and custom Python scripts with a single, fast, self-contained
C++ tool that skips ECC generation. Builds are near-instant — only bottleneck
is the external mkisofs call. `cdiextractor` also doubles as a Dreambeam
replacement for CRC32 scanning and game identification.

**This is a debugging/iteration tool, not a mastering tool.** It is useful for
modders and translators of classic (2000–2002) Dreamcast games who need
repetitive rebuild-and-test cycles. Less useful for postmarket games (most are
data/data 45k LBA, not yet supported).

## Tools

| Tool | Purpose |
|------|---------|
| `cdiextractor` | Extract files, boot sector, and CRC32 scan lists from CDI images |
| `cdibuilder` | Build CDI images from directories (audio/data mode) |

## Usage Cases

- **AI agents / automated pipelines**: `cdiextractor` extracts files from most
  CDI images without needing Windows tools (Wine + Iso7z + 7z is more reliable
  for some images but cannot extract IP.BIN). Generates a Dreambeam-format CRC32
  diff that can be matched against the Dreambeam database for game
  identification — consuming fewer tokens than raw file analysis.

- **Boot sector extraction**: `cdiextractor` can pull the real IP.BIN from
  outside the ISO9660 filesystem, which Iso7z cannot do.

- **Fast rebuilds**: `cdibuilder` builds CDI images significantly faster than
  mkdcdisc (which is slow due to its toolchain layer). Useful when you need
  dozens of rebuild-and-test cycles during translation or modding work.

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
