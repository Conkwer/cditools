/*
    IP.BIN/BOOT.BIN SB Hacker - CLI version (non-interactive)
    Ported to C++ with --output-dir and --quiet flags matching binhack.py

    Usage: binhack32 <binary> <bootsector> <lba> --output-dir <dir> [--quiet]

    Based on FamilyGuy's binhack32 v1.0.0.5 (GPLv3)
*/

#include "binhack.hpp"
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

using namespace std;

static void print_usage(const char *prog) {
    cerr << "Usage: " << prog
         << " <binary> <bootsector> <lba> --output-dir <dir> [--quiet]"
         << endl;
}

// mkdir -p equivalent
static void mkdir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return;
    string cmd = "mkdir -p \"";
    cmd += path;
    cmd += "\"";
    system(cmd.c_str());
}

int main(int argc, char *argv[]) {
    bool quiet = false;
    const char *output_dir = nullptr;
    const char *bootname = nullptr;
    const char *ipname = nullptr;
    unsigned int lba = 0;

    // Parse CLI arguments
    int i = 1;
    int pos = 0;
    while (i < argc) {
        if (strcmp(argv[i], "--output-dir") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) output_dir = argv[++i];
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            // debug flag accepted but no-op in release
        } else {
            switch (pos) {
                case 0: bootname = argv[i]; break;
                case 1: ipname = argv[i]; break;
                case 2: lba = (unsigned int)strtoul(argv[i], nullptr, 10); break;
            }
            pos++;
        }
        i++;
    }

    if (pos < 3 || !output_dir) {
        print_usage(argv[0]);
        return 1;
    }

    if (!quiet) cout << "*** binhack32 CLI (aarch64) ***" << endl;

    // Open input files
    fstream boot;
    ifstream ipbin;

    boot.open(bootname, ios::binary | ios::in | ios::out);
    if (boot.fail()) {
        cerr << "Error opening " << bootname << "." << endl;
        return 2;
    }

    ipbin.open(ipname, ios::in | ios::binary);
    if (ipbin.fail()) {
        cerr << "Error opening " << ipname << "." << endl;
        return 1;
    }

    unsigned int bootsize = filesize(boot);
    unsigned int hackoffset = searchHackOffset(boot, bootsize);

    // Create output directory
    mkdir_p(output_dir);

    // Output paths
    string iphak_path = string(output_dir) + "/" + (strrchr(ipname, '/') ? strrchr(ipname, '/') + 1 : ipname);
    string boot_name_only = strrchr(bootname, '/') ? strrchr(bootname, '/') + 1 : bootname;
    string bootout_path = string(output_dir) + "/" + boot_name_only;

    // Copy IP.BIN to output
    {
        ifstream src(ipname, ios::binary);
        ofstream dst(iphak_path, ios::binary);
        dst << src.rdbuf();
    }

    // Hack the bootsector (IP.BIN)
    {
        ofstream iphak(iphak_path, ios::binary | ios::in | ios::out);
        if (iphak.fail()) {
            cerr << "Error creating " << iphak_path << "." << endl;
            return 5;
        }
        hackBootStrap(iphak, bootsize, boot);
    }

    if (isWinCE(boot, hackoffset)) {
        if (!quiet) cout << "Found Windows CE" << endl;
        // WinCE: only hack IP.BIN, don't touch the binary
        // Disable WinCE OS flag in IP.BIN
        {
            ofstream iphak(iphak_path, ios::binary | ios::in | ios::out);
            iphak.seekp(BOOTSECTOR_HACK_OS_OFFSET, ios::beg);
            iphak.write(BOOTSECTOR_OS_FLAG, 1);
        }
    } else {
        // Katana: hack the boot binary LBA
        if (!quiet) cout << "Found Katana binary" << endl;
        hackKatanaBootBinary(boot, bootsize, lba);

        // Write patched boot binary
        {
            ofstream b_out(bootout_path, ios::binary);
            boot.seekg(0, ios::beg);
            char buf[4096];
            while (boot.read(buf, sizeof(buf)) || boot.gcount() > 0) {
                b_out.write(buf, boot.gcount());
            }
        }
    }

    boot.close();
    ipbin.close();

    if (!quiet) cout << "Done." << endl;
    return 0;
}
