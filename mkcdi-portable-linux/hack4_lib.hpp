#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace hack4 {

struct Config {
    uint32_t old_pos = 0xafc8;  // 45000
    uint32_t new_pos = 0x2db6;  // 11702
    bool hack1 = true;
    bool hack2 = true;
    bool unprotect = true;
};

// Apply patches to binary data in-memory. Returns true if any patches were applied.
bool apply_patches(std::vector<uint8_t>& data, const Config& config);

// Read file, apply patches, write back. Returns true if patched.
bool process_file(const std::string& filename, const Config& config);

// Expand wildcard pattern to matching files
std::vector<std::string> expand_wildcard(const std::string& pattern);

// Convenience: process all .BIN files in a directory (excluding IP.BIN),
// first unprotect, then HACK1+HACK2 with given LBA.
void process_directory(const std::string& dir_path, uint32_t lba);

} // namespace hack4
