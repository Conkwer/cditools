#include "binhack32_lib.hpp"
#include "binhack.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/stat.h>
#include <cstdlib>

namespace binhack32_lib {

static void mkdir_p(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) return;
#ifdef _WIN32
    std::string cmd = "mkdir \"";
    cmd += path;
    cmd += "\"";
#else
    std::string cmd = "mkdir -p \"";
    cmd += path;
    cmd += "\"";
#endif
    system(cmd.c_str());
}

bool process(const std::string& boot_path,
             const std::string& ipbin_path,
             unsigned int lba,
             const std::string& output_dir,
             bool quiet) {
    // Open input files
    std::fstream boot;
    std::ifstream ipbin;

    boot.open(boot_path, std::ios::binary | std::ios::in | std::ios::out);
    if (boot.fail()) {
        std::cerr << "binhack32: Error opening " << boot_path << "\n";
        return false;
    }

    ipbin.open(ipbin_path, std::ios::in | std::ios::binary);
    if (ipbin.fail()) {
        std::cerr << "binhack32: Error opening " << ipbin_path << "\n";
        return false;
    }

    unsigned int bootsize = filesize(boot);
    unsigned int hackoffset = searchHackOffset(boot, bootsize);

    // Create output directory
    mkdir_p(output_dir.c_str());

    // Build output paths
    const char* ipbase = strrchr(ipbin_path.c_str(), '/');
    ipbase = ipbase ? ipbase + 1 : ipbin_path.c_str();
    std::string iphak_path = output_dir + "/" + ipbase;

    const char* bootbase = strrchr(boot_path.c_str(), '/');
    bootbase = bootbase ? bootbase + 1 : boot_path.c_str();
    std::string bootout_path = output_dir + "/" + bootbase;

    // Copy IP.BIN to output
    {
        std::ifstream src(ipbin_path, std::ios::binary);
        std::ofstream dst(iphak_path, std::ios::binary);
        dst << src.rdbuf();
    }

    // Hack the bootsector (IP.BIN)
    {
        std::ofstream iphak(iphak_path, std::ios::binary | std::ios::in | std::ios::out);
        if (iphak.fail()) {
            std::cerr << "binhack32: Error creating " << iphak_path << "\n";
            return false;
        }
        hackBootStrap(iphak, bootsize, boot);
    }

    bool wince = (hackoffset != (unsigned int)-1) && isWinCE(boot, hackoffset);

    if (wince) {
        if (!quiet) std::cout << "binhack32: Found Windows CE\n";
        // WinCE: only hack IP.BIN, don't touch the binary
        // Disable WinCE OS flag in IP.BIN
        {
            std::ofstream iphak(iphak_path, std::ios::binary | std::ios::in | std::ios::out);
            iphak.seekp(BOOTSECTOR_HACK_OS_OFFSET, std::ios::beg);
            iphak.write(BOOTSECTOR_OS_FLAG, 1);
        }
    } else {
        if (!quiet) {
            if (hackoffset == (unsigned int)-1)
                std::cout << "binhack32: Katana binary (no LBA patch needed)\n";
            else
                std::cout << "binhack32: Found Katana binary\n";
        }
        if (hackoffset != (unsigned int)-1)
            hackKatanaBootBinary(boot, hackoffset, lba);

        // Write patched boot binary
        if (boot.clear(), boot.seekg(0, std::ios::beg), true) {
            std::ofstream b_out(bootout_path, std::ios::binary);
            char buf[4096];
            while (boot.read(buf, sizeof(buf)) || boot.gcount() > 0) {
                b_out.write(buf, boot.gcount());
            }
        }
    }

    boot.close();
    ipbin.close();

    return true;
}

} // namespace binhack32_lib
