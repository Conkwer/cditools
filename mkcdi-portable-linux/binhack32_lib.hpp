#pragma once

#include <cstdint>
#include <string>

namespace binhack32_lib {

// Run the full binhack32 pipeline:
// 1. Opens boot binary and IP.BIN
// 2. Copies IP.BIN to output directory
// 3. Applies bootstrap hack to IP.BIN
// 4. For Katana: writes LBA into boot binary and copies to output
// 5. For WinCE: sets OS flag in IP.BIN, doesn't touch boot binary
//
// Output files are written to output_dir/ with the same basenames.
// Returns true on success.
bool process(const std::string& boot_path,
             const std::string& ipbin_path,
             unsigned int lba,
             const std::string& output_dir,
             bool quiet = false);

} // namespace binhack32_lib
