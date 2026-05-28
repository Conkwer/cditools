CDI Extractor
=============

Unpacks individual files from DiscJuggler (.cdi) Dreamcast images.
Also works as a drop-in replacement for Dreambeam's directory scanner.

Build
-----

  Linux:   g++ -std=c++17 -O2 -o cdiextractor cdiextractor.cpp
  Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -o cdiextractor.exe cdiextractor.cpp

Usage
-----

  cdiextractor <image.cdi | directory> [options]

If the input is a .cdi file, the tool extracts the filesystem and (optionally)
the boot sector. If the input is a directory, it scans all files inside and
produces a Dreambeam-compatible hash listing.

Options
-------

  -l, --list        List files inside the CDI (no extraction)
  -x, --extract     Extract files from the CDI
  -b, --bootsector  Also extract the boot sector (IP.BIN) as bootsector.bin
  -s, --scan        Generate a Dreambeam _dbscan.txt with CRC32 hashes
  -m, --match <db>  Match scan hash against Dreambeam database at <db>
  -C <dir>          Output directory for extracted files (default: data/)
  -a, --all         Include subdirectories when listing
  -h, --help        Show help

No flags (drag-and-drop)
------------------------

Dropping a .cdi file onto the executable does everything at once:
extract all files + boot sector + scan list, output to ./data/

Dropping a folder does the same but without CDI extraction:
creates a _dbscan.txt next to the folder.

Examples
--------

  # Unpack a CDI image (extract files + bootsector + scan)
  cdiextractor game.cdi

  # List what's inside without extracting
  cdiextractor game.cdi -l

  # Extract to a specific folder
  cdiextractor game.cdi -x -b -C ./game_files

  # Generate a scan list and match against Dreambeam database
  cdiextractor game.cdi -s -m /path/to/dreambeam

  # Scan an already-extracted folder
  cdiextractor ./game_files

  # Scan a folder and match against database
  cdiextractor ./game_files -m /path/to/dreambeam

Output files
------------

  data/1ST_READ.BIN     Game executable
  data/TEST.TXT ...     Other files from the disc
  data/bootsector.bin   IP.BIN boot sector (16 sectors, with -b)
  game_dbscan.txt       Dreambeam hash listing (with -s, saved next to CDI)

Supported image types
---------------------

  audio+data    One audio session + one data session (most common, ~90%)
  data+data     Two data sessions (modern images, both tracks merged)

  Audio tracks are skipped. Only the data portion is extracted.

Limitations
-----------

  - Multi-session discs with files spread across sessions may miss files
  - DJ v4 images may not be fully supported
  - Timezone offsets in file dates are ignored
