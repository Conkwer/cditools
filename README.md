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
| `dreamdiff` | Compare Dreambeam database files and generate color-coded diff reports |
| `cdi7z` | Open-source 7-Zip CDI format plugin (Linux) — single-step extraction |

## Usage Cases

- **AI agents / automated pipelines**: `cdiextractor` unpacks any CDI without
  Windows-only tools (no Wine, no closed-source Iso7z plugin, no 7z). Produces
  a Dreambeam-format CRC32 list that can be diffed directly against the
  Dreambeam database — one hash comparison identifies the game, no token-heavy
  file-by-file analysis needed. Simpler than grepping the database blindly
  because you can produce the same hash format from any image and get an
  exact match (or a deliberate mismatch).

- **Boot sector extraction**: `cdiextractor` can pull the real IP.BIN from
  outside the ISO9660 filesystem, which Iso7z cannot do.

- **Fast rebuilds**: `cdibuilder` skips ECC generation (emulators and ODEs
  don't need it), so builds are near-instant beyond the mkisofs call. Compared
  to mkdcdisc (which computes ECC and has a complex dependency chain that makes
  it painful to compile, especially on Windows), `cdibuilder` is a single .cpp
  file — trivial to modify and rebuild. Ideal for drop-in replacement in
  Windows automation pipelines (e.g. upgrading Lazyboot's Python-based mkcdi
  step for broader compatibility and speed).

- **Dreambeam replacement**: if the original Dreambeam doesn't run on your
  system (the older version have compatibility problems and modern Dreambeam
  was still in beta), `cdiextractor -s` produces the same `_dbscan.txt` format.
  Drop that file into your database and use `dreamdiff` (included) to compare
  it against existing entries — preserving the Dreambeam ecosystem without
  requiring the original GUI.

- **Lightweight / legacy environments**: both tools compile with g++ on any
  system with a C++17 compiler. Windows binaries can target WinXP, making them
  usable in minimal VirtualBox appliances where installing Python or Wine is
  impractical. The only external dependency is mkisofs/genisoimage.

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

## cdi7z — 7-Zip CDI plugin (Linux)

Open-source format plugin replacing Iso7z. Single-step extraction: `7z x image.cdi`
extracts files directly (no intermediate ISO step).

```bash
cd cdi7z/bin
LD_LIBRARY_PATH=. ./7z l image.cdi          # list files
LD_LIBRARY_PATH=. ./7z x image.cdi -o./out  # extract to folder
```

Features: IP.BIN display in Comment, ISO9660 dates/attributes, audio/data and
data/data support. Linux x86_64 only — Windows users can use the
[Iso7z](https://www.tc4shell.com/en/7zip/iso7z/) plugin instead.

## Limitations

- `cdibuilder` audio/data mode only (data/data coming)
- No ECC/EDC generation (emulators and ODEs don't require it)
- No CDDA audio track support
