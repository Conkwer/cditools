# mkcdi — Native Linux Dreamcast CDI Builder

A native Linux toolchain for building bootable Dreamcast CDI images.
No Wine required. For Katana, WinCE, and KallistiOS binaries.

## Quick Start

```bash
# 1. Put your game files in data/
# 2. Run:
./mkcdi.sh data/ mygame.cdi

# Or manually:
./system/cdibuilder -d data/ -o mygame.cdi -l 11702 -t audio
```

## System Requirements

- **Linux**: Any modern distribution (Debian 12+, Ubuntu 24.04 LTS)
- **Dependencies**: `genisoimage` (for mkisofs), `python3` (for Python tools)
- **Install**: `sudo apt install genisoimage python3`

## Tools Included

### Native C++ Binaries (pre-compiled ELF64)

| Tool | Purpose |
|------|---------|
| `cdibuilder` | Full CDI builder. DJ v3.5 headers. Supports Audio/Data + Data/Data. |
| `fill` | ISO padder. Pads session 1 to target LBA. `fill file.iso 45000` |
| `hack4` | Binary LBA patcher. Replaces GD-ROM LBA references for CD-R. |
| `scramble` | BOOTROM-compatible binary scrambler. `-d` flag for descramble. |
| `redump2cdi` | Redump CUE → DiscJuggler CDI converter. |
| `binhack32` | IP.BIN patcher (legacy). |

### Python Scripts

| Tool | Purpose |
|------|---------|
| `iso2cdi.py` | ISO → CDI converter. Pure Python (zlib + base64), no dependencies. |
| `bincon.py` | WinCE binary converter. Strips PE header, patches IP.BIN byte 0x3E. |
| `binhack.py` | IP.BIN patcher. Python rewrite with modern CLI. |
| `hack4.py` | LBA patcher. Python rewrite of kikuchan's hack4 v1.5 (2001). |

### Preconfigured IP.BIN Templates (`precon/`)

| File | Use Case |
|------|----------|
| `katana.bin` | Generic Katana SDK IP.BIN |
| `kos.bin` | KallistiOS homebrew IP.BIN |
| `wince.bin` | Windows CE IP.BIN |
| `lodoss-5167.bin` | Record of Lodoss War (1NOSDC.BIN format) |

## Build Pipelines

### Katana — Commercial Game (Audio/Data)
```bash
# From GDI-extracted data
cd data/
hack4 -0 -w 1ST_READ.BIN          # Patch LBA 45000 → 11702
cd ..
cdibuilder -d data/ -o game.cdi -l 11702 -t audio
```

### Katana — Data/Data (No LBA Patching)
```bash
cdibuilder -d data/ -o game.cdi -l 45000 -t data
```

### WinCE Game
```bash
python3 bincon.py data/0WINCEOS.BIN data/IP.BIN --replace
cdibuilder -d data/ -o game.cdi -l 11702 -t audio
```

### KallistiOS Homebrew (ELF)
```bash
objcopy -O binary data/game.elf /tmp/unscrambled.bin
./scramble /tmp/unscrambled.bin data/1ST_READ.BIN
cp precon/kos.bin data/IP.BIN
cdibuilder -d data/ -o game.cdi -l 11702 -t audio
```

### KallistiOS Homebrew (Pre-scrambled)
```bash
cdibuilder -d data/ -o game.cdi -l 11702 -t audio
```

## Building from Source

All C++ tools compile with GCC/Clang:

```bash
g++ -std=c++17 -O2 cdibuilder.cpp -o cdibuilder
g++ -std=c++17 -O2 fill.cpp -o fill
g++ -std=c++17 -O2 hack4.cpp -o hack4
gcc -O2 scramble.c -o scramble
```

## Verification

Standard 7z cannot read CDI files. Use the CDI-enabled 7z plugin from this repo:

```bash
# Using cdi7z (included in cditools repo)
cd ../cdi7z/bin
LD_LIBRARY_PATH=. ./7z l /path/to/game.cdi
LD_LIBRARY_PATH=. ./7z x /path/to/game.cdi -o./extracted
```

cdi7z is included in this repo at `../cdi7z/`. It is also mirrored in the Dreamcast Knowledge Base at `deepdream/tools/cdi7z/`.

## Limitations

- **No ECC/EDC generation** — images are for testing and emulation, not final CD-R mastering
- For release-quality images, use Lazyboot (Windows) or make45k (Windows/Wine)
- `iso2cdi.py` produces minimal-headered CDIs (simpler than cdibuilder's full DJ v3.5 headers)

## Platform Notes

- **This is a native Linux toolchain** — no Wine required
- All binaries compiled on Debian 12 x86-64
- Python scripts work on any platform with Python 3.8+

## Credits

- **Conkwer** — cdibuilder, lzlite, make45k, toolchain integration
- **kikuchan** — original hack4 (2001)
- **Darc** — Redump2CDI
- **FamilyGuy** — original 45000 LBA concept
- **KallistiOS team** — scramble/unscramble
- **Dreamcast homebrew community**
