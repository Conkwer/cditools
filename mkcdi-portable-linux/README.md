# mkcdi-portable-linux — All-in-One CDI Builder

Single C++17 binary. Handles KOS (ELF), Katana, and WinCE (bincon) builds.
**100% standalone** — embedded libisofs replaces mkisofs. Zero external deps
beyond standard system libraries.

## Quick Start

```bash
make          # Linux x86-64 (static, 3.3 MB)
make arm64    # Linux aarch64 (static, 3.2 MB)
make win      # Windows x64 cross-compile (static, 3.6 MB)
make win32    # Windows x86 cross-compile (static, 3.4 MB)

./mkcdi -V "Game" -d ./data
```

Drag-and-drop: `./mkcdi /path/to/game` builds with smart defaults
(volume-id from dirname, fast mode, output next to input).

Pre-built static binaries included for all four platforms.
External deps: none (cdi4dc optional for LBA 11702 with ECC — `-f` uses
built-in cdibuilder).

## Usage

```
mkcdi [options] [directory]
mkcdi directory     (drag-and-drop quick build)

  -V, --volume-id NAME   ISO Volume Identifier (required)
  -l, --lba LBA          Session 2 LBA (default: 11702, 45000 data/data)
  -b, --binary BIN       Boot binary filename (default: 1ST_READ.BIN)
  -d, --data-dir DIR     Directory with game data (default: ./data)
  -o, --output CDI       Output CDI filename (default: NAME.cdi)
  -s, --sort FILE        File sorting order (optional)
  -f, --fast             Fast mode: built-in cdibuilder (no ECC)
  -t, --timestamp        Append build timestamp to output filename
  -q, --quiet            Suppress info messages
  -h, --help             Show this help
  --version              Show version info
  --kos                  KallistiOS mode: skip hack4/binhack
  --nohack               Skip hack4/binhack32/bincon (pre-patched)
  --logo                 Inject logo.mr into IP.BIN (Katana)
  --dummy                Pad disc to optimal capacity (LBA 11702 only)
```

## Pipeline

```
KOS ELF?  → elf_parser → scramble → 1ST_READ.BIN
Katana?   → hack4 (unprotect + LBA) → binhack32
WinCE?    → hack4 → bincon → binhack32 → logoinsert (wince.mr)
          → libisofs (in-process ISO) → cdi4dc or cdibuilder → CDI
```

IP.BIN templates (katana/wince/kos/lodoss) and MR logos are embedded.

## Build Requirements

- **Linux**: g++, Python 3 (asset embedding), libpthread
- **Windows cross-compile**: x86_64-w64-mingw32-g++, windres
- **aarch64 cross-compile**: aarch64-linux-gnu-g++
- **No runtime deps**: embedded libisofs + win-iconv (Windows)

## Source Layout

| File | Role |
|------|------|
| `mkcdi_portable.cpp` | CLI, config loading, pipeline orchestration |
| `libisofs_wrapper.cpp` | In-process ISO creation (replaces mkisofs) |
| `hack4_lib.*` | Binary patcher (unprotect + LBA relocate) |
| `bincon.*` | WinCE → Katana binary converter |
| `binhack32_lib.*` | IP.BIN bootstrap + boot binary LBA hacker |
| `logoinsert_lib.*` | MR logo injector (IP.BIN offset 0x3820) |
| `cdibuilder_lib.*` | Fast CDI builder (golden blob headers) |
| `binhack.*` | Core binhack32 + 11500-byte bootstrap data |
| `scramble.*` | GD-ROM scrambler (Marcus Comstedt) |
| `elf_parser.*` | SH-4 ELF parser |
| `embedded_assets.*` | IP.BIN templates + MR logos (auto-generated) |
| `third_party/libisofs/` | libisofs 1.5.7 (libburnia, GPLv2+) |
| `third_party/libisofs/winpatch/` | win-iconv + MinGW compat headers |

## Credits

| Module | Author | Notes |
|------|--------|-------|
| `pipeline orchestrator` / lzlite / Lazyboot | Conkwer | GPLv3 |
| `hack4` (C++/Python) | Conkwer | GPLv3; Inspired by kikuchan's original hack4 (2001); independent implementation |
| `binhack32` | FamilyGuy (v1.0.0.5, 2011-2014), SiZiOUS (optimization) | GPLv3; bugs fixed (tail, false CD001, stream failbit) |
| `bincon` (C) | unknown | GPLv3; original Windows binary |
| `bincon.py` | Conkwer | GPLv3; Python port of the original |
| `cdi4dc` | [big_fury]SiZiOUS (v0.5b, 2021) | GPLv3 |
| `cdibuilder` | Conkwer | GPLv3; C++ CDI builder, used with `--fast` or as fallback for non-11702 LBAs |
| `iso2cdi.py` | Conkwer | GPLv3; based on PSX-Planet script (unknown author) |
| `scramble` | Marcus Comstedt | Public Domain |
| `elf_parser` | finixbit (2018), Colton Pawielski (2022-2023) | MIT |
| `logoinsert` | Conkwer | GPLv3; simple MR logo injector |
| `doomer` | Conkwer | GPLv3; dummy/padding file creator |
| `elf2bin` | finixbit, Pawielski, Marcus Comstedt | Merges elf_parser (MIT) + scramble (Public Domain) into one CLI tool |
| `mkisofs` wrapper | J. Schilling / E. Youngdale | GPL; genisoimage |

## License

Components carry their original licenses (~GPLv3)
