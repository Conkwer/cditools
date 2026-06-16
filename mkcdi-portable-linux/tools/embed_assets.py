#!/usr/bin/env python3
"""Generate embedded_assets.h/.cpp from binary asset files."""

import os
import sys

ASSETS_DIR = os.path.join(os.path.dirname(__file__), "..")

ASSETS = {
    "katana":       "precon/katana.bin",
    "wince":        "precon/wince.bin",
    "kos":          "precon/kos.bin",
    "lodoss_5167":  "precon/lodoss-5167.bin",
    "wince_mr":     "wince.mr",
}

SRC_DIR = "/home/claude/projects/mkcdi-linux/system"


def bytes_to_hex(data, indent=4):
    """Format bytes as C hex array."""
    lines = []
    prefix = " " * indent
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_bytes = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"{prefix}{hex_bytes},")
    return "\n".join(lines)


def generate_header(assets_info):
    """Generate embedded_assets.h"""
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
        "namespace embedded {",
        ""
    ]

    for name, info in assets_info.items():
        lines.append(f"// {info['desc']}")
        lines.append(f"extern const uint8_t {name}[{info['size']}];")
        lines.append(f"constexpr size_t {name}_size = {info['size']};")
        lines.append("")

    lines.append("} // namespace embedded")
    return "\n".join(lines)


def generate_source(assets_info):
    """Generate embedded_assets.cpp"""
    lines = [
        '#include "embedded_assets.h"',
        "",
        "namespace embedded {",
        ""
    ]

    for name, info in assets_info.items():
        lines.append(f"// {info['desc']}")
        lines.append(f"const uint8_t {name}[{info['size']}] = {{")
        lines.append(bytes_to_hex(info['data']))
        lines.append("};")
        lines.append("")

    lines.append("} // namespace embedded")
    return "\n".join(lines)


def main():
    assets_info = {}

    for var_name, rel_path in ASSETS.items():
        full_path = os.path.join(SRC_DIR, rel_path)
        if not os.path.exists(full_path):
            print(f"ERROR: Asset not found: {full_path}", file=sys.stderr)
            sys.exit(1)

        with open(full_path, "rb") as f:
            data = f.read()

        assets_info[var_name] = {
            "data": data,
            "size": len(data),
            "desc": os.path.basename(rel_path),
        }
        print(f"  {var_name}: {len(data)} bytes ({os.path.basename(rel_path)})")

    # Write header
    header_path = os.path.join(ASSETS_DIR, "embedded_assets.h")
    with open(header_path, "w") as f:
        f.write(generate_header(assets_info))
    print(f"Wrote: {header_path}")

    # Write source
    source_path = os.path.join(ASSETS_DIR, "embedded_assets.cpp")
    with open(source_path, "w") as f:
        f.write(generate_source(assets_info))
    print(f"Wrote: {source_path}")

    print("Done.")


if __name__ == "__main__":
    main()
