#include "embedded_assets.h"
#include "hack4_lib.hpp"
#include "bincon.hpp"
#include "binhack32_lib.hpp"
#include "logoinsert_lib.hpp"
#include "cdibuilder_lib.hpp"
#include "libisofs_wrapper.hpp"
#include "scramble.h"
#include "elf_parser.hpp"
#include "elf.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// ============================================================
// Config & CLI
// ============================================================

struct AppConfig {
    std::string volume_id;   // ISO 9660 Volume Identifier (formerly --romname)
    unsigned int lba = 11702;
    std::string binary = "1ST_READ.BIN";
    std::string data_dir = "./data";
    std::string output;
    std::string sort_file;
    bool quiet = false;
    bool fast_mode = false;
    bool dummy = false;
    bool nohack = false;
    bool logo = false;
    bool timestamp = false;
    bool kos = false;
    bool drag_drop = false;  // set when invoked with single positional arg
    std::string script_dir;
};

static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options] [directory]\n"
        << "       " << prog << " directory     (drag-and-drop quick build)\n\n"
        << "Options:\n"
        << "  -V, --volume-id NAME   ISO Volume Identifier (required)\n"
        << "  -l, --lba LBA          Session 2 LBA (default: 11702)\n"
        << "  -b, --binary BIN       Boot binary filename (default: 1ST_READ.BIN)\n"
        << "  -d, --data-dir DIR     Directory with game data (default: ./data)\n"
        << "  -o, --output CDI       Output CDI filename (default: NAME.cdi)\n"
        << "  -s, --sort FILE        Sort file for ISO creation (optional)\n"
        << "  -f, --fast             Fast mode: built-in cdibuilder (no ECC)\n"
        << "  -t, --timestamp        Append build timestamp to filename\n"
        << "  -q, --quiet            Suppress info messages\n"
        << "  -h, --help             Show this help\n"
        << "  --version              Show version info\n"
        << "  --kos                  KallistiOS mode: skip hack4/binhack\n"
        << "  --nohack               Skip hack4/binhack32/bincon (pre-patched)\n"
        << "  --logo                 Inject logo.mr into IP.BIN (Katana)\n"
        << "  --dummy                Pad disc to optimal capacity (LBA 11702)\n"
        << "\n  --romname NAME         Deprecated alias for --volume-id\n"
        << "\nDrag-and-drop: invoke with a single directory argument for a quick\n"
        << "build with defaults (--volume-id from dirname, --fast, --lba 11702).\n";
}

static void print_version() {
    std::cout << "mkcdi v1.0 — Dreamcast CDI builder\n"
              << "Embedded libisofs 1.5.7, win-iconv\n"
              << "https://github.com/Conkwer/cditools\n";
}

// Simple config file parser: reads KEY=VALUE lines
static void load_config(const std::string& conf_path, AppConfig& cfg) {
    std::ifstream f(conf_path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        // Skip comments
        if (line.empty() || line[0] == '#') continue;
        // Find =
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // Strip quotes
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
            value = value.substr(1, value.size() - 2);

        if (key == "ROMNAME" || key == "VOLUME_ID") cfg.volume_id = value;
        else if (key == "LBA") cfg.lba = std::stoul(value);
        else if (key == "BINARY") cfg.binary = value;
        else if (key == "DATA_DIR") cfg.data_dir = value;
        else if (key == "OUTPUT") cfg.output = value;
        else if (key == "SORT_FILE") cfg.sort_file = value;
        else if (key == "KOS" && (value == "true" || value == "1")) cfg.kos = true;
        else if (key == "NOHACK" && (value == "true" || value == "1")) cfg.nohack = true;
        else if (key == "LOGO" && (value == "true" || value == "1")) cfg.logo = true;
        else if (key == "DUMMY" && (value == "true" || value == "1")) cfg.dummy = true;
        else if (key == "FAST" && (value == "true" || value == "1")) cfg.fast_mode = true;
        else if (key == "QUIET" && (value == "true" || value == "1")) cfg.quiet = true;
        else if (key == "TIMESTAMP" && (value == "true" || value == "1")) cfg.timestamp = true;
    }
}

static AppConfig parse_args(int argc, char* argv[]) {
    AppConfig cfg;

    // Determine script directory
#ifdef _WIN32
    char exe_path[MAX_PATH] = {};
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) > 0) {
        cfg.script_dir = fs::path(exe_path).parent_path().string();
    } else if (argv[0][0] != '\0') {
        cfg.script_dir = fs::absolute(fs::path(argv[0])).parent_path().string();
    }
#else
    char exe_path[4096] = {};
    if (readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1) > 0) {
        cfg.script_dir = fs::path(exe_path).parent_path().string();
    } else if (argv[0][0] == '/') {
        cfg.script_dir = fs::path(argv[0]).parent_path().string();
    } else {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            cfg.script_dir = (fs::path(cwd) / fs::path(argv[0]).parent_path()).string();
        }
    }
#endif

    // Load config if present
    std::string conf_path = cfg.script_dir + "/mkcdi.conf";
    load_config(conf_path, cfg);

    // Parse CLI args (override config)
    bool explicit_data_dir = false;
    bool explicit_volume_id = false;
    std::string positional;  // single positional arg = drag-drop data dir

    auto is_flag = [](const char* arg, const char* longname, const char* shortname) -> bool {
        return strcmp(arg, longname) == 0 || (shortname && strcmp(arg, shortname) == 0);
    };

    int i = 1;
    while (i < argc) {
        // --help / -h
        if (is_flag(argv[i], "--help", "-h")) {
            print_usage(argv[0]);
            exit(0);
        }
        // --version
        else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
        }
        // --volume-id / -V  (and deprecated --romname)
        else if ((is_flag(argv[i], "--volume-id", "-V") || strcmp(argv[i], "--romname") == 0) && i + 1 < argc) {
            cfg.volume_id = argv[++i];
            explicit_volume_id = true;
            if (strcmp(argv[i-1], "--romname") == 0 && !cfg.quiet)
                std::cerr << "Note: --romname is deprecated, use -V/--volume-id\n";
        }
        // --lba / -l
        else if (is_flag(argv[i], "--lba", "-l") && i + 1 < argc) {
            cfg.lba = std::stoul(argv[++i]);
        }
        // --binary / -b
        else if (is_flag(argv[i], "--binary", "-b") && i + 1 < argc) {
            cfg.binary = argv[++i];
        }
        // --data-dir / -d
        else if (is_flag(argv[i], "--data-dir", "-d") && i + 1 < argc) {
            cfg.data_dir = argv[++i];
            explicit_data_dir = true;
        }
        // --output / -o
        else if (is_flag(argv[i], "--output", "-o") && i + 1 < argc) {
            cfg.output = argv[++i];
        }
        // --sort / -s
        else if (is_flag(argv[i], "--sort", "-s") && i + 1 < argc) {
            cfg.sort_file = argv[++i];
        }
        // --fast / -f
        else if (is_flag(argv[i], "--fast", "-f")) {
            cfg.fast_mode = true;
        }
        // --timestamp / -t
        else if (is_flag(argv[i], "--timestamp", "-t")) {
            cfg.timestamp = true;
        }
        // --quiet / -q
        else if (is_flag(argv[i], "--quiet", "-q")) {
            cfg.quiet = true;
        }
        // mode flags (no short form)
        else if (strcmp(argv[i], "--kos") == 0) { cfg.kos = true; }
        else if (strcmp(argv[i], "--nohack") == 0) { cfg.nohack = true; }
        else if (strcmp(argv[i], "--logo") == 0) { cfg.logo = true; }
        else if (strcmp(argv[i], "--dummy") == 0) { cfg.dummy = true; }
        // Positional arg (not starting with -)
        else if (argv[i][0] != '-') {
            if (positional.empty()) {
                positional = argv[i];
            } else {
                std::cerr << "Error: unexpected extra argument: " << argv[i] << "\n";
                print_usage(argv[0]);
                exit(1);
            }
        }
        else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            exit(1);
        }
        i++;
    }

    // Drag-and-drop: single positional arg, no explicit data-dir
    if (!positional.empty() && !explicit_data_dir) {
        struct stat st;
        if (stat(positional.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            std::cerr << "Error: '" << positional << "' is not a directory\n";
            exit(1);
        }
        cfg.data_dir = positional;
        cfg.drag_drop = true;

        // Derive volume-id from dirname if not explicitly given
        if (!explicit_volume_id) {
            std::string name = fs::path(positional).filename().string();
            // Sanitize to valid ISO volume-id chars (uppercase A-Z, 0-9, _), max 32
            std::string vol;
            for (char c : name) {
                if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
                    if (vol.size() < 32) vol += c;
                }
            }
            if (!vol.empty()) cfg.volume_id = vol;
        }
        // Default output next to input folder
        if (cfg.output.empty()) {
            std::string parent = fs::path(positional).parent_path().string();
            std::string name   = fs::path(positional).filename().string();
            if (parent.empty()) parent = ".";
            cfg.output = parent + "/" + name + ".cdi";
        }
        // Default: fast mode, LBA 11702
        if (!cfg.fast_mode) cfg.fast_mode = true;
    }

    return cfg;
}

// ============================================================
// Helpers
// ============================================================

static std::string build_timestamp() {
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", tm);
    return buf;
}

#ifdef _WIN32
static std::string display_path(const std::string& s) {
    // Normalize mixed slashes for display. Windows understands both,
    // but A:\/foo looks weird. Replace all forward slashes with backslashes.
    std::string r = s;
    for (char& c : r) if (c == '/') c = '\\';
    return r;
}
#else
static std::string display_path(const std::string& s) { return s; }
#endif

static bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static void write_embedded_file(const uint8_t* data, size_t size, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: cannot write " << path << "\n";
        exit(1);
    }
    out.write(reinterpret_cast<const char*>(data), size);
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// ============================================================
// Pipeline Steps
// ============================================================

// Returns true if an ELF was found and converted to 1ST_READ.BIN
static bool step_kos_elf(const AppConfig& cfg, std::string& binary) {
    // Count .elf files in data dir
    std::vector<std::string> elf_files;
    try {
        for (const auto& entry : fs::directory_iterator(cfg.data_dir)) {
            if (!entry.is_regular_file()) continue;
            std::string fn = entry.path().filename().string();
            if (fn.size() > 4 && fn.substr(fn.size() - 4) == ".elf") {
                elf_files.push_back(entry.path().string());
            }
        }
    } catch (...) { return false; }

    if (elf_files.size() != 1) {
        if (elf_files.size() > 1) {
            std::cerr << "Error: Multiple .elf files found in " << cfg.data_dir
                      << "/. Keep only one.\n";
            exit(1);
        }
        return false;  // no elf files
    }

    if (!cfg.quiet) {
        std::cout << "==> KOS ELF detected: "
                  << fs::path(elf_files[0]).filename().string() << "\n";
    }

    // Read ELF
    std::ifstream elf_file(elf_files[0], std::ios::binary);
    if (!elf_file) {
        std::cerr << "Error opening: " << elf_files[0] << "\n";
        exit(1);
    }
    std::vector<char> elf_data((std::istreambuf_iterator<char>(elf_file)),
                                std::istreambuf_iterator<char>());

    // Parse ELF
    auto parser_opt = elfparser::Parser::Load(elf_data);
    if (!parser_opt || !*parser_opt) {
        std::cerr << "Error parsing ELF\n";
        exit(1);
    }

    // Extract binary
    std::vector<char> unscrambled;
    if (!(*parser_opt)->fill_bin(unscrambled) || unscrambled.empty()) {
        std::cerr << "Error extracting binary from ELF\n";
        exit(1);
    }

    // Scramble
    std::vector<char> scrambled = scramble(unscrambled);

    // Write 1ST_READ.BIN
    std::string out_path = cfg.data_dir + "/1ST_READ.BIN";
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error writing: " << out_path << "\n";
        exit(1);
    }
    out.write(scrambled.data(), scrambled.size());
    if (!cfg.quiet)
        std::cout << "  elf2bin: " << scrambled.size() << " bytes -> 1ST_READ.BIN\n";

    // Delete the ELF
    fs::remove(elf_files[0]);

    binary = "1ST_READ.BIN";
    return true;
}

static void step_ipbin_template(const AppConfig& cfg, const std::string& binary, bool kos) {
    std::string ip_path = cfg.data_dir + "/IP.BIN";

    if (binary == "1NOSDC.BIN") {
        write_embedded_file(embedded::lodoss_5167, embedded::lodoss_5167_size, ip_path);
        if (!cfg.quiet) std::cout << "IP.BIN: lodoss-5167 template\n";
    } else if (kos && !file_exists(ip_path)) {
        write_embedded_file(embedded::kos, embedded::kos_size, ip_path);
        if (!cfg.quiet) std::cout << "IP.BIN: kos template (boots3)\n";
    } else if (!file_exists(ip_path)) {
        if (binary == "0WINCEOS.BIN") {
            write_embedded_file(embedded::wince, embedded::wince_size, ip_path);
            if (!cfg.quiet) std::cout << "IP.BIN: wince template (boots2)\n";
        } else {
            write_embedded_file(embedded::katana, embedded::katana_size, ip_path);
            if (!cfg.quiet) std::cout << "IP.BIN: katana template (boots1)\n";
        }
    } else {
        if (!cfg.quiet) std::cout << "IP.BIN: using existing\n";
    }
}

static void step_hack4(const AppConfig& cfg) {
    if (!cfg.quiet) std::cout << "Patching binary...\n";
    hack4::process_directory(cfg.data_dir, cfg.lba);
}

static void step_bincon(const AppConfig& cfg) {
    if (cfg.binary != "0WINCEOS.BIN") return;

    std::string wince_path = cfg.data_dir + "/0WINCEOS.BIN";
    std::string ipbin_path = cfg.data_dir + "/IP.BIN";

    if (!file_exists(wince_path) || !file_exists(ipbin_path)) return;

    if (!cfg.quiet) std::cout << "  bincon (WinCE -> Katana-style)...\n";

    try {
        auto wince_data = read_file(wince_path);

        if (bincon::is_already_converted(wince_data)) {
            if (!cfg.quiet) std::cout << "  bincon: file already converted, skipping\n";
            return;
        }

        auto converted = bincon::convert(wince_data);

        // Patch IP.BIN for WinCE
        auto ipbin_data = read_file(ipbin_path);
        bincon::patch_ipbin_for_wince(ipbin_data);

        // Write both files back
        write_file(wince_path, converted);
        write_file(ipbin_path, ipbin_data);
    } catch (const std::exception& e) {
        std::cerr << "bincon error: " << e.what() << "\n";
        exit(1);
    }
}

static void step_binhack32(const AppConfig& cfg) {
    if (!file_exists(cfg.data_dir + "/IP.BIN")) return;

    std::string boot_path = cfg.data_dir + "/" + cfg.binary;
    std::string ipbin_path = cfg.data_dir + "/IP.BIN";

    if (!cfg.quiet)
        std::cout << "  binhack32 " << cfg.binary << " IP.BIN LBA=" << cfg.lba << "...\n";

    // Create temp directory (same filesystem as data dir to allow rename)
    std::string tmpdir = cfg.data_dir + "/.mkcdi_binhack_tmp";
    fs::create_directories(tmpdir);

    if (!binhack32_lib::process(boot_path, ipbin_path, cfg.lba, tmpdir, cfg.quiet)) {
        std::cerr << "binhack32: processing failed\n";
        fs::remove_all(tmpdir);
        exit(1);
    }

    // Move hacked files back to data/
    std::string hacked_boot = tmpdir + "/" + cfg.binary;
    std::string hacked_ip = tmpdir + "/IP.BIN";

    if (file_exists(hacked_boot)) {
        fs::rename(hacked_boot, boot_path);
    }
    if (file_exists(hacked_ip)) {
        fs::rename(hacked_ip, ipbin_path);
    }

    fs::remove_all(tmpdir);
}

static void step_logoinsert(const AppConfig& cfg) {
    // WinCE always gets wince.mr
    if (cfg.binary == "0WINCEOS.BIN") {
        if (!cfg.quiet) std::cout << "  logoinsert (inject wince.mr)...\n";
        auto ipbin = read_file(cfg.data_dir + "/IP.BIN");
        std::vector<uint8_t> logo(embedded::wince_mr,
                                   embedded::wince_mr + embedded::wince_mr_size);
        logoinsert::inject_logo(ipbin, logo);
        write_file(cfg.data_dir + "/IP.BIN", ipbin);
        return;
    }

    // Katana only with --logo
    if (cfg.logo) {
        if (!cfg.quiet) std::cout << "  logoinsert (inject logo.mr)...\n";
        auto ipbin = read_file(cfg.data_dir + "/IP.BIN");
        std::vector<uint8_t> logo(embedded::logo_mr,
                                   embedded::logo_mr + embedded::logo_mr_size);
        logoinsert::inject_logo(ipbin, logo);
        write_file(cfg.data_dir + "/IP.BIN", ipbin);
    }
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    AppConfig cfg = parse_args(argc, argv);

    // Validate
    if (cfg.volume_id.empty()) {
        std::cerr << "Error: --volume-id is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Resolve data dir to absolute path
    try {
        cfg.data_dir = fs::canonical(cfg.data_dir).string();
    } catch (...) {
        std::cerr << "Error: data directory '" << cfg.data_dir << "' not found\n";
        return 1;
    }

    if (cfg.output.empty()) {
        cfg.output = cfg.volume_id + ".cdi";
    }

    std::string build_ver = build_timestamp();
    if (!cfg.quiet) std::cout << "Build: " << build_ver << "\n";

    if (cfg.timestamp) {
        cfg.output = cfg.volume_id + "-" + build_ver + ".cdi";
    }

    // ============================================================
    // Pipeline
    // ============================================================

    bool is_kos = cfg.kos;
    std::string binary = cfg.binary;

    // Step 1: KOS ELF detection (matches mkcdi.sh: if exactly one .elf exists → KOS=true)
    if (step_kos_elf(cfg, binary)) {
        is_kos = true;
    }

    // Step 2: Auto-detect binary
    if (file_exists(cfg.data_dir + "/0WINCEOS.BIN")) {
        binary = "0WINCEOS.BIN";
    }
    if (!file_exists(cfg.data_dir + "/" + binary)) {
        std::cerr << "Error: boot binary '" << binary << "' not found in "
                  << cfg.data_dir << "/\n";
        return 1;
    }
    cfg.binary = binary; // update config

    // Step 3: IP.BIN template selection
    step_ipbin_template(cfg, binary, is_kos);

    // Step 4: hack4 + binhack (skip for KOS or --nohack)
    if (is_kos || cfg.nohack) {
        if (!cfg.quiet) {
            std::cout << "==> Skipping hack4/binhack ("
                      << (is_kos ? "KOS" : "--nohack") << ")\n";
        }
    } else {
        step_hack4(cfg);
        step_bincon(cfg);
        step_binhack32(cfg);
    }

    // Step 5: Logo injection (always after binhack)
    step_logoinsert(cfg);

    // Step 6: Dummy file
    if (cfg.dummy) {
        // Remove stale dummy
        fs::remove(cfg.data_dir + "/0.0");

        if (cfg.lba > 11702) {
            std::cout << "Note: --dummy disabled for LBA " << cfg.lba
                      << " (requires LBA 11702)\n";
        } else {
            // Calculate dummy size
            uint64_t datasize = 0;
            try {
                for (const auto& entry : fs::recursive_directory_iterator(cfg.data_dir)) {
                    if (entry.is_regular_file()) {
                        datasize += entry.file_size();
                    }
                }
            } catch (...) {}

            const uint64_t DISC_SIZE = 712841213;
            const uint64_t CDDA_OFFSET = 7340032;
            int64_t maxsize = DISC_SIZE - CDDA_OFFSET;
            int64_t dummy_size = maxsize - (int64_t)datasize;

            if (dummy_size > 0) {
                if (!cfg.quiet) {
                    std::cout << "  Data: " << (datasize / 1024 / 1024) << " MB, "
                              << "Dummy: " << (dummy_size / 1024 / 1024) << " MB\n";
                }
                // Create dummy file by seeking to size-1 and writing a byte
                std::string dummy_path = cfg.data_dir + "/0.0";
                std::ofstream dfile(dummy_path, std::ios::binary);
                if (dfile) {
                    dfile.seekp(dummy_size - 1);
                    dfile.write("", 1);
                }
            } else {
                std::cout << "Warning: --dummy: data already fills disc ("
                          << (datasize / 1024 / 1024) << " MB)\n";
            }
        }
    }

    // Step 7: mkisofs
    std::string iso_file;
    {
        std::string base = cfg.output;
        if (base.size() > 4 && base.substr(base.size() - 4) == ".cdi") {
            iso_file = base.substr(0, base.size() - 4) + ".iso";
        } else {
            iso_file = base + ".iso";
        }
    }

    {
        if (!cfg.quiet) std::cout << "Creating ISO (LBA=" << cfg.lba << ")...\n";

        if (!create_dreamcast_iso(cfg.data_dir, iso_file, cfg.lba,
                                  cfg.volume_id, cfg.sort_file, cfg.quiet)) {
            std::cerr << "Error: ISO creation failed\n";
            return 1;
        }
    }

    // Step 8: ISO -> CDI
    {
        if (!cfg.quiet) std::cout << "Converting ISO to CDI...\n";

        if (cfg.fast_mode) {
            if (!cfg.quiet) std::cout << "  cdibuilder -I " << display_path(iso_file)
                                      << " -o " << display_path(cfg.output)
                                      << " -l " << cfg.lba
                                      << " -t audio -V '" << cfg.volume_id << "' (fast)\n";
            cdibuilder_lib::build_from_iso(iso_file, cfg.output, cfg.lba,
                                           cfg.volume_id.c_str(), false);
        } else if (cfg.lba == 11702) {
            // Use cdi4dc for proper ECC/EDC
            std::string cdi4dc_path = cfg.script_dir + "/cdi4dc";
            if (file_exists(cdi4dc_path)) {
                if (!cfg.quiet) std::cout << "  cdi4dc " << display_path(iso_file) << " "
                                          << display_path(cfg.output)
                                          << " (audio/data, LBA 11702)\n";
                char cmd[4096];
                snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\"",
                         cdi4dc_path.c_str(), iso_file.c_str(), cfg.output.c_str());
                int ret = system(cmd);
                if (ret != 0) {
                    std::cerr << "Error: cdi4dc failed with code " << ret << "\n";
                    fs::remove(iso_file);
                    return 1;
                }
            } else {
                // cdi4dc not found, fall back to cdibuilder
                if (!cfg.quiet) std::cout << "  cdi4dc not found, using cdibuilder\n";
                cdibuilder_lib::build_from_iso(iso_file, cfg.output, cfg.lba,
                                               cfg.volume_id.c_str(), false);
            }
        } else {
            if (!cfg.quiet)
                std::cout << "Note: LBA " << cfg.lba
                          << " — cdi4dc only handles LBA 11702. Using cdibuilder.\n";
            cdibuilder_lib::build_from_iso(iso_file, cfg.output, cfg.lba,
                                           cfg.volume_id.c_str(), false);
        }
    }

    // Step 9: Cleanup
    fs::remove(iso_file);

    std::cout << "\nDone: " << display_path(cfg.output) << "\n";

#ifdef _WIN32
    // On drag-and-drop, the console window auto-closes. Pause so user can
    // read the result. Only pause when we own the console (not launched
    // from an existing cmd.exe). --quiet suppresses this.
    if (cfg.drag_drop && !cfg.quiet) {
        DWORD procs = 0;
        if (GetConsoleProcessList(&procs, 1) <= 1) {
            std::cout << "\nPress any key to exit...\n";
            system("pause > nul");
        }
    }
#endif

    return 0;
}
