#include "hack4_lib.hpp"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
namespace hack4 {

// Read file into a byte vector
static std::vector<uint8_t> read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open file: " + filename);

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        throw std::runtime_error("Cannot read file: " + filename);

    return buffer;
}

// Write byte vector to file
static void write_file(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open file for writing: " + filename);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// Convert 4 bytes to a uint32_t (little-endian)
static uint32_t bytes_to_uint32(const std::vector<uint8_t>& data, size_t offset) {
    return (uint32_t)(data[offset + 3] << 24) | (data[offset + 2] << 16) |
           (data[offset + 1] << 8) | data[offset];
}

// Convert uint32_t to 4 bytes (little-endian)
static void uint32_to_bytes(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset]     = value & 0xFF;
    data[offset + 1] = (value >> 8) & 0xFF;
    data[offset + 2] = (value >> 16) & 0xFF;
    data[offset + 3] = (value >> 24) & 0xFF;
}

// Check if a pattern exists at a specific position
static bool check_pattern(const std::vector<uint8_t>& data, size_t offset,
                          const std::vector<uint8_t>& pattern) {
    if (offset + pattern.size() > data.size()) return false;
    for (size_t i = 0; i < pattern.size(); i++)
        if (data[offset + i] != pattern[i]) return false;
    return true;
}

bool apply_patches(std::vector<uint8_t>& data, const Config& config) {
    bool patched = false;

    // Unprotect mode pattern: CD E4 43 6A -> 09 00 09 00
    const std::vector<uint8_t> unprotect_pattern = {0xCD, 0xE4, 0x43, 0x6A};
    const std::vector<uint8_t> unprotect_replace  = {0x09, 0x00, 0x09, 0x00};

    if (config.unprotect) {
        for (size_t i = 0; i <= data.size() - unprotect_pattern.size(); i++) {
            if (check_pattern(data, i, unprotect_pattern)) {
                std::copy(unprotect_replace.begin(), unprotect_replace.end(), data.begin() + i);
                patched = true;
            }
        }
    }

    // HACK1/HACK2: Search for old position values
    if (config.hack1 || config.hack2) {
        for (size_t i = 0; i <= data.size() - 4; i++) {
            uint32_t value = bytes_to_uint32(data, i);

            // HACK1: oldpos + 166 -> newpos + 166
            if (config.hack1 && value == config.old_pos + 166) {
                uint32_to_bytes(data, i, config.new_pos + 166);
                patched = true;
            }

            // HACK2: oldpos + 150 -> newpos + 150
            if (config.hack2 && value == config.old_pos + 150) {
                uint32_to_bytes(data, i, config.new_pos + 150);
                patched = true;
            }
        }
    }

    return patched;
}

bool process_file(const std::string& filename, const Config& config) {
    try {
        auto data = read_file(filename);
        bool patched = apply_patches(data, config);
        if (patched) {
            write_file(filename, data);
        }
        return patched;
    } catch (const std::exception& e) {
        std::cerr << "Error processing " << filename << ": " << e.what() << "\n";
        return false;
    }
}

std::vector<std::string> expand_wildcard(const std::string& pattern) {
    std::vector<std::string> files;
    try {
        fs::path path(pattern);
        std::string dir = path.parent_path().string();
        std::string filename_pattern = path.filename().string();
        if (dir.empty()) dir = ".";

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string fn = entry.path().filename().string();
                bool match = false;
                if (filename_pattern == "*") {
                    match = true;
                } else if (filename_pattern.find('*') != std::string::npos) {
                    // Simple wildcard: match prefix before *
                    std::string prefix = filename_pattern.substr(0, filename_pattern.find('*'));
                    match = (fn.size() >= prefix.size() && fn.compare(0, prefix.size(), prefix) == 0);
                } else {
                    match = (fn == filename_pattern);
                }
                if (match) files.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error expanding wildcard: " << e.what() << "\n";
    }
    return files;
}

void process_directory(const std::string& dir_path, uint32_t lba) {
    // Collect .BIN files (case-sensitive), exclude IP.BIN
    std::vector<std::string> bin_files;
    try {
        for (const auto& entry : fs::directory_iterator(dir_path)) {
            if (!entry.is_regular_file()) continue;
            std::string fn = entry.path().filename().string();
            // Match *.BIN but not IP.BIN
            if (fn.size() > 4 && fn.substr(fn.size() - 4) == ".BIN" && fn != "IP.BIN") {
                bin_files.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << "\n";
        return;
    }

    if (bin_files.empty()) return;

    // Pass 1: unprotect
    {
        Config cfg;
        cfg.hack1 = false;
        cfg.hack2 = false;
        cfg.unprotect = true;
        for (const auto& f : bin_files)
            process_file(f, cfg);
    }

    // Pass 2: HACK1 + HACK2 with LBA
    {
        Config cfg;
        cfg.new_pos = lba;
        cfg.hack1 = true;
        cfg.hack2 = true;
        cfg.unprotect = false;
        for (const auto& f : bin_files)
            process_file(f, cfg);
    }
}

} // namespace hack4
