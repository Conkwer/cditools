#pragma once

#include <cstdint>
#include <vector>

namespace bincon {

// Check if a WinCE binary has already been converted.
// Compares the last two 0x800-byte chunks — if identical, it's already converted.
bool is_already_converted(const std::vector<uint8_t>& data);

// Convert a WinCE binary (0WINCEOS.BIN) to Katana-style format.
// Strips the first 0x800 bytes and appends a copy of the last 0x800 bytes.
// Input is the full original binary. Returns the converted binary.
std::vector<uint8_t> convert(const std::vector<uint8_t>& input);

// Patch IP.BIN for WinCE: sets byte at offset 0x3E to 0x30 (removes WinCE OS flag).
// Modifies data in-place.
void patch_ipbin_for_wince(std::vector<uint8_t>& ipbin);

} // namespace bincon
