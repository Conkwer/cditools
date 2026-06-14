#include "bincon.hpp"

#include <cstring>
#include <stdexcept>

namespace bincon {

bool is_already_converted(const std::vector<uint8_t>& data) {
    if (data.size() < 0x1000) {
        // Too small to check — assume not converted
        return false;
    }

    // Compare last 0x800 bytes with the 0x800 bytes before them
    const uint8_t* chunk1 = data.data() + data.size() - 0x1000; // last 0x1000..0x800
    const uint8_t* chunk2 = data.data() + data.size() - 0x800;  // last 0x800

    return (memcmp(chunk1, chunk2, 0x800) == 0);
}

std::vector<uint8_t> convert(const std::vector<uint8_t>& input) {
    if (input.size() <= 0x800) {
        throw std::runtime_error("bincon: input file too small (must be > 2048 bytes)");
    }

    if (is_already_converted(input)) {
        // Already converted — return as-is (skip first 0x800)
        return std::vector<uint8_t>(input.begin() + 0x800, input.end());
    }

    // Remove first 0x800 bytes, then append last 0x800 bytes again
    // Original logic:
    //   fread from offset 0x800 => lsize - 0x800 bytes
    //   then fwrite last 0x800 bytes (from lsize - 0x1000 offset in original)
    size_t stripped_size = input.size() - 0x800;        // everything after header
    std::vector<uint8_t> result;
    result.reserve(stripped_size + 0x800);

    // Copy from offset 0x800 to end
    result.insert(result.end(), input.begin() + 0x800, input.end());

    // Append last 0x800 bytes again (from original offset lsize - 0x1000)
    size_t last_chunk_start = input.size() - 0x1000;
    result.insert(result.end(), input.begin() + last_chunk_start,
                  input.begin() + last_chunk_start + 0x800);

    return result;
}

void patch_ipbin_for_wince(std::vector<uint8_t>& ipbin) {
    if (ipbin.size() > 0x3E) {
        ipbin[0x3E] = 0x30;
    }
}

} // namespace bincon
