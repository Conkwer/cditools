#pragma once

#include <cstdint>
#include <string>

namespace cdibuilder_lib {

// Build a CDI image from an ISO file.
// Uses the same golden blob templates and layout as the standalone cdibuilder.
//
// Parameters:
//   iso_path    - path to the input ISO file
//   output_cdi  - path to the output CDI file
//   lba         - session 2 start LBA (11702 for audio/data, 45000 for data/data)
//   volume      - volume label (max 32 chars)
//   data_mode   - true for data/data, false for audio/data
//
// Throws std::runtime_error on failure.
void build_from_iso(const std::string& iso_path,
                    const std::string& output_cdi,
                    int lba,
                    const char* volume,
                    bool data_mode = false);

// Build a CDI image from a data directory (runs mkisofs internally).
// Same as build_from_iso but creates the ISO first via system("mkisofs ...").
void build_from_dir(const std::string& input_dir,
                    const std::string& output_cdi,
                    int lba,
                    const char* volume,
                    bool data_mode = false);

} // namespace cdibuilder_lib
