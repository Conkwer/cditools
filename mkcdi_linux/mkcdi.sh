#!/bin/bash
# mkcdi.sh — Quick Dreamcast CDI builder for Linux
# Usage: ./mkcdi.sh <data_dir> [output.cdi] [lba] [type]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SYSTEM_DIR="$SCRIPT_DIR/system"
DATA_DIR="${1:-data}"
OUTPUT="${2:-game.cdi}"
LBA="${3:-11702}"
TYPE="${4:-audio}"

if [ ! -d "$DATA_DIR" ]; then
    echo "Error: data directory '$DATA_DIR' not found"
    echo "Usage: $0 <data_dir> [output.cdi] [lba] [type]"
    echo "  type: audio (Audio/Data) or data (Data/Data)"
    echo "  lba:  11702 for audio/data, 45000 for data/data"
    exit 1
fi

# Auto-detect binary type
BINARY=""
if [ -f "$DATA_DIR/1ST_READ.BIN" ]; then BINARY="1ST_READ.BIN"; fi
if [ -f "$DATA_DIR/0WINCEOS.BIN" ]; then BINARY="0WINCEOS.BIN"; fi
if [ -f "$DATA_DIR/1NOSDC.BIN" ]; then BINARY="1NOSDC.BIN"; fi

# Handle ELF files
ELF_COUNT=$(ls "$DATA_DIR"/*.elf 2>/dev/null | wc -l)
if [ "$ELF_COUNT" -eq 1 ]; then
    echo "KOS ELF detected, converting to 1ST_READ.BIN..."
    ELF_FILE=$(ls "$DATA_DIR"/*.elf | head -1)
    objcopy -O binary "$ELF_FILE" /tmp/mkcdi_unscrambled.bin
    "$SYSTEM_DIR/scramble" /tmp/mkcdi_unscrambled.bin "$DATA_DIR/1ST_READ.BIN"
    cp "$SCRIPT_DIR/precon/kos.bin" "$DATA_DIR/IP.BIN"
    rm -f "$ELF_FILE" /tmp/mkcdi_unscrambled.bin
    BINARY="1ST_READ.BIN"
    echo "ELF converted successfully"
elif [ "$ELF_COUNT" -gt 1 ]; then
    echo "Error: Multiple .elf files found. Keep only one."
    exit 1
fi

if [ -z "$BINARY" ]; then
    echo "Warning: No binary found (1ST_READ.BIN, 0WINCEOS.BIN, 1NOSDC.BIN, or .elf)"
fi

# Build CDI
echo "Building $TYPE CDI at LBA $LBA..."
echo "  Data: $DATA_DIR"
echo "  Binary: ${BINARY:-none}"
echo "  Output: $OUTPUT"

"$SYSTEM_DIR/cdibuilder" -d "$DATA_DIR" -o "$OUTPUT" -l "$LBA" -t "$TYPE"

echo ""
echo "Done: $OUTPUT"
echo "Verify: 7z l $OUTPUT"
