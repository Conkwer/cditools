#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace logoinsert {

// Inject logo data into an IP.BIN buffer at offset 0x3820.
// Returns false if the buffer is too small. Modifies data in-place.
bool inject_logo(std::vector<uint8_t>& ipbin_data, const std::vector<uint8_t>& logo_data);

// Convenience: read logo from file and inject into IP.BIN file.
// Returns true on success.
bool inject_logo_file(const std::string& logo_path, const std::string& ipbin_path);

} // namespace logoinsert
