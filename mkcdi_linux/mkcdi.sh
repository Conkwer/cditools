#!/bin/bash
# mkcdi.sh — Quick Dreamcast CDI builder for Linux
# Usage: ./mkcdi.sh <data_dir> [output.cdi] [options]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SYSTEM_DIR="$SCRIPT_DIR/system"

# Architecture detection: use -arm64 binaries on aarch64
ARCH=""
case "$(uname -m)" in
    aarch64|arm64) ARCH="-arm64" ;;
esac
# Resolve tool path: tries $name$ARCH first, falls back to $name
tool() { local t="$SYSTEM_DIR/$1$ARCH"; [ -x "$t" ] && echo "$t" || echo "$SYSTEM_DIR/$1"; }

# Defaults
DATA_DIR="data"
OUTPUT=""
LBA="11702"
TYPE="audio"
PATCH_BINARY=true
DRY_RUN=false

usage() {
    cat <<EOF
Usage: $0 <data_dir> [options]

Options:
  -o <file>     Output CDI file (default: <volume>.cdi)
  -l <lba>      Session 2 LBA: 11702 for audio/data, 45000 for data/data
  -t <type>     Image type: audio (Audio/Data) or data (Data/Data)
  -n, --no-patch  Skip binhack/hack4 (use for KOS homebrew or pre-patched binaries)
  --dry-run     Show what would be done without building

Examples:
  $0 data/ -o game.cdi                     # Katana game at audio/data 11702
  $0 data/ -o game.cdi -n                  # KOS homebrew (skip patching)
  $0 data/ -o game.cdi -l 45000 -t data    # Data/Data at 45000
  $0 data/ -o game.cdi -l 45000 -t data -n # KOS with Data/Data
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT="$2"; shift 2 ;;
        -l) LBA="$2"; shift 2 ;;
        -t) TYPE="$2"; shift 2 ;;
        -n|--no-patch) PATCH_BINARY=false; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
        -h|--help) usage ;;
        *) DATA_DIR="$1"; shift ;;
    esac
done

if [ ! -d "$DATA_DIR" ]; then
    echo "Error: data directory '$DATA_DIR' not found"
    usage
fi

# Auto-detect binary type and SDK
BINARY=""
SDK="unknown"
if [ -f "$DATA_DIR/1ST_READ.BIN" ]; then BINARY="1ST_READ.BIN"; fi
if [ -f "$DATA_DIR/0WINCEOS.BIN" ]; then BINARY="0WINCEOS.BIN"; SDK="wince"; fi
if [ -f "$DATA_DIR/1NOSDC.BIN" ]; then BINARY="1NOSDC.BIN"; fi

# Handle ELF files (KOS homebrew)
ELF_COUNT=$(ls "$DATA_DIR"/*.elf 2>/dev/null | wc -l)
if [ "$ELF_COUNT" -eq 1 ]; then
    SDK="kos"
    PATCH_BINARY=false  # KOS never needs patching
    echo "==> KOS ELF detected"
    if ! $DRY_RUN; then
        ELF_FILE=$(ls "$DATA_DIR"/*.elf | head -1)
        objcopy -O binary "$ELF_FILE" /tmp/mkcdi_unscrambled.bin
        $(tool scramble) /tmp/mkcdi_unscrambled.bin "$DATA_DIR/1ST_READ.BIN"
        cp "$SCRIPT_DIR/precon/kos.bin" "$DATA_DIR/IP.BIN"
        rm -f "$ELF_FILE" /tmp/mkcdi_unscrambled.bin
        BINARY="1ST_READ.BIN"
    fi
    echo "    -> 1ST_READ.BIN (scrambled), kos.bin IP.BIN"
elif [ "$ELF_COUNT" -gt 1 ]; then
    echo "Error: Multiple .elf files found. Keep only one."
    exit 1
fi

# Detect KOS pre-scrambled binary (has kos.bin or no binhack needed flag)
if [ "$SDK" != "wince" ] && [ -f "$DATA_DIR/IP.BIN" ]; then
    # Check if it's a KOS IP.BIN (kos.bin template)
    KOS_MD5=$(md5sum "$SCRIPT_DIR/precon/kos.bin" 2>/dev/null | cut -d' ' -f1)
    IP_MD5=$(md5sum "$DATA_DIR/IP.BIN" 2>/dev/null | cut -d' ' -f1)
    if [ "$KOS_MD5" = "$IP_MD5" ]; then
        SDK="kos"
        PATCH_BINARY=false
        echo "==> KOS IP.BIN detected (skipping binhack/hack4)"
    fi
fi

# Set default output name
if [ -z "$OUTPUT" ]; then
    VOL=$(head -c 32 "$DATA_DIR/IP.BIN" 2>/dev/null | strings | head -1 | tr -d ' ' || echo "game")
    [ -z "$VOL" ] && VOL="game"
    OUTPUT="${VOL}.cdi"
fi

# Apply binhack + hack4 for Katana/WinCE
if $PATCH_BINARY && [ "$SDK" != "kos" ]; then
    if [ -n "$BINARY" ]; then
        echo "==> Patching binary for LBA $LBA..."

        # hack4: patch LBA references in the binary
        if [ -f "$(tool hack4)" ]; then
            echo "    hack4 -0 -w $BINARY (45000 -> $LBA)"
            $DRY_RUN || (cd "$DATA_DIR" && $(tool hack4) -0 -w "$BINARY")
        fi

        # binhack: patch IP.BIN (region flags + reset trick)
        if [ -f "$(tool binhack32)" ] && [ -f "$DATA_DIR/IP.BIN" ]; then
            echo "    binhack $BINARY IP.BIN $LBA"
            $DRY_RUN || $(tool binhack32) "$DATA_DIR/$BINARY" "$DATA_DIR/IP.BIN" "$LBA" --output-dir "$DATA_DIR/" --quiet
        fi

        # WinCE: convert binary format
        if [ "$BINARY" = "0WINCEOS.BIN" ] && [ -f "$SYSTEM_DIR/bincon.py" ]; then
            echo "    bincon.py (WinCE -> Katana-style)"
            $DRY_RUN || python3 "$SYSTEM_DIR/bincon.py" "$DATA_DIR/0WINCEOS.BIN" "$DATA_DIR/IP.BIN" --replace
        fi
    fi
elif [ "$SDK" = "kos" ]; then
    echo "==> KOS homebrew: skipping binhack/hack4"
elif ! $PATCH_BINARY; then
    echo "==> --no-patch: skipping binhack/hack4"
fi

# Ensure IP.BIN exists
if [ ! -f "$DATA_DIR/IP.BIN" ]; then
    if [ "$BINARY" = "0WINCEOS.BIN" ]; then
        cp "$SCRIPT_DIR/precon/wince.bin" "$DATA_DIR/IP.BIN"
    elif [ "$SDK" = "kos" ]; then
        cp "$SCRIPT_DIR/precon/kos.bin" "$DATA_DIR/IP.BIN"
    else
        cp "$SCRIPT_DIR/precon/katana.bin" "$DATA_DIR/IP.BIN"
    fi
    echo "==> Using generic IP.BIN"
fi

# Build CDI
echo "==> Building $TYPE CDI at LBA $LBA..."
echo "    SDK: $SDK, Binary: ${BINARY:-none}"
echo "    Output: $OUTPUT"

if $DRY_RUN; then
    echo "    [dry-run: skipping build]"
else
    $(tool cdibuilder) -d "$DATA_DIR" -o "$OUTPUT" -l "$LBA" -t "$TYPE"
fi

echo ""
echo "Done: $OUTPUT"
echo "Verify: 7z l $OUTPUT"
