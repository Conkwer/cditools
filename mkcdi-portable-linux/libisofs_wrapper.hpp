#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Create an ISO 9660 image with Dreamcast boot sector (IP.BIN in system area).
// Replaces: mkisofs -C 0,LBA -V VOLUME -G IP.BIN -exclude IP.BIN -l -J -r -o output data_dir
//
// Returns true on success, false on failure (prints errors to stderr).
bool create_dreamcast_iso(const std::string& data_dir,
                          const std::string& output_iso,
                          unsigned int lba,
                          const std::string& volume_name,
                          const std::string& sort_file = "",
                          bool quiet = false);
