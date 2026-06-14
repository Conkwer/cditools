#include "logoinsert_lib.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

namespace logoinsert {

static std::vector<uint8_t> read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize))
        throw std::runtime_error("Failed to read file: " + filename);

    return buffer;
}

static void write_file(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open file for writing: " + filename);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

bool inject_logo(std::vector<uint8_t>& ipbin_data, const std::vector<uint8_t>& logo_data) {
    if (ipbin_data.size() < 0x3820 + logo_data.size()) {
        std::cerr << "logoinsert: IP.BIN too small to hold logo data\n";
        return false;
    }

    if (logo_data.size() > 8192) {
        std::cerr << "logoinsert: warning — image is larger than 8192 bytes "
                  << "and may corrupt a normal IP.BIN, inserting anyway!\n";
    }

    std::copy(logo_data.begin(), logo_data.end(), ipbin_data.begin() + 0x3820);
    return true;
}

bool inject_logo_file(const std::string& logo_path, const std::string& ipbin_path) {
    try {
        auto logo_data = read_file(logo_path);
        auto ipbin_data = read_file(ipbin_path);

        if (!inject_logo(ipbin_data, logo_data))
            return false;

        write_file(ipbin_path, ipbin_data);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "logoinsert error: " << e.what() << "\n";
        return false;
    }
}

} // namespace logoinsert
