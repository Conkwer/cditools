# mkcdi-portable-linux — All-in-One CDI Builder

Single C++17 binary replacing the mkcdi.sh bash pipeline. Merges hack4,
binhack32, bincon, elf2bin, logoinsert, doomer, and cdibuilder logic into
one executable.

External dependencies: `mkisofs` (genisoimage) and `cdi4dc` — called via
`system()`.

## Quick Start

```bash
# Build from source
make          # Linux (g++)
make win      # Windows cross-compile (MinGW-w64)

# Run
./mkcdi --romname "Game Name" --data-dir ./data --output game.cdi
```

Pre-built binaries included: `mkcdi` (Linux x86-64), `mkcdi.exe` (Windows x64, static).

## Usage

```
mkcdi [options]

Options:
  --romname NAME    Game name / volume label (required)
  --lba LBA         Session 2 LBA (default: 11702 audio/data, 45000 data/data)
  --binary BIN      Boot binary filename (default: 1ST_READ.BIN)
  --data-dir DIR    Directory with game data (default: ./data)
  --output CDI      Output CDI filename (default: NAME.cdi)
  --sort FILE       Sort file for mkisofs (optional)
  --kos             KallistiOS mode: skip hack4/binhack, use kos.bin IP.BIN
  --nohack          Skip hack4/binhack32/bincon (pre-patched binaries)
  --logo            Inject logo.mr into IP.BIN (Katana only; WinCE auto-injects wince.mr)
  --dummy           Pad disc to optimal capacity (LBA 11702 only)
  --fast            Fast mode: use cdibuilder (no ECC, for quick testing)
  --timestamp       Append build timestamp to output filename
  --quiet           Suppress info messages
  -h, --help        Show help
```

## Pipeline

```
KOS ELF?  → elf_parser → scramble → 1ST_READ.BIN
Katana?   → hack4 (unprotect + LBA) → binhack32 → IP.BIN + boot hacks
WinCE?    → hack4 → bincon → binhack32 → logoinsert (wince.mr)
          → mkisofs → cdi4dc (LBA 11702) or cdibuilder (any LBA)
```

IP.BIN templates (katana/wince/kos/lodoss) and MR logos are embedded — no
external template files needed.

## Build Requirements

- **Linux**: g++ (C++17), Python 3 (for asset embedding)
- **Windows cross-compile**: x86_64-w64-mingw32-g++, windres
- **Runtime**: mkisofs/genisoimage, cdi4dc (for LBA 11702 with ECC)

## Source Layout

| File | Role |
|------|------|
| `mkcdi_portable.cpp` | CLI parsing, config loading, pipeline orchestration |
| `hack4_lib.*` | Binary patcher (unprotect + LBA relocate) |
| `bincon.*` | WinCE → Katana binary converter |
| `binhack32_lib.*` | IP.BIN bootstrap + boot binary LBA hacker |
| `logoinsert_lib.*` | MR logo injector (offset 0x3820 in IP.BIN) |
| `cdibuilder_lib.*` | Fast CDI builder (golden blob headers, any LBA) |
| `binhack.*` | Core binhack32 functions + 11500-byte bootstrap data |
| `scramble.*` | GD-ROM scrambler (Marcus Comstedt algorithm) |
| `elf_parser.*` / `elf.h` | SH-4 ELF parser → raw binary extraction |
| `doomer.cpp` | Dummy/padding file creator |
| `embedded_assets.*` | Auto-generated: IP.BIN templates + MR logos |
| `tools/embed_assets.py` | Reads binary assets, generates `embedded_assets.*` |

## License

Components carry their original licenses:
- binhack32: GPLv3 (FamilyGuy)
- bincon: GPL (dopefish)
- elf_parser: MIT (finixbit / Colton Pawielski)
- scramble: Public Domain (Marcus Comstedt)
- cdibuilder templates: derived from DiscJuggler CDI format
