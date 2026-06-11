#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <cstring>

namespace fs = std::filesystem;

static constexpr uint32_t ISO_LOGICAL_SECTOR_SIZE = 2048;   // ISO9660 logical block size
static constexpr uint32_t CD_PREGAP_SECTORS       = 150;    // Track pre-gap
static constexpr uint32_t CD_LEADIN_SECTORS       = 4500;   // Session lead-in (~60s @ 75 sectors/s)
static constexpr uint32_t CD_LEADOUT_SECTORS      = 6750;   // Session lead-out (~90s @ 75 sectors/s)
static constexpr uint32_t SESSION_OVERHEAD_SECTORS =
    CD_PREGAP_SECTORS + CD_LEADIN_SECTORS + CD_LEADOUT_SECTORS; // = 11,400

struct Options {
    std::string filename;
    uint64_t targetLBA = 45000;       // default Dreamcast-friendly LBA
    uint32_t sectorSize = ISO_LOGICAL_SECTOR_SIZE;
    uint32_t overheadSectors = SESSION_OVERHEAD_SECTORS;
    bool dryRun = false;
};

static bool parseUint64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
#if defined(_MSC_VER)
    unsigned long long val = std::strtoull(s.c_str(), &end, 10);
#else
    unsigned long long val = std::strtoull(s.c_str(), &end, 10);
#endif
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = static_cast<uint64_t>(val);
    return true;
}

static bool parseUint32(const std::string& s, uint32_t& out) {
    uint64_t tmp = 0;
    if (!parseUint64(s, tmp)) return false;
    if (tmp > std::numeric_limits<uint32_t>::max()) return false;
    out = static_cast<uint32_t>(tmp);
    return true;
}

static void printUsage(const char* argv0) {
    std::cout <<
    "Usage:\n"
    "  " << argv0 << " <file.iso> [target_lba] [--dry-run] [--sector-size=2048] [--overhead=11400]\n\n"
    "Description:\n"
    "  Pads <file.iso> with zeros so that a multisession build will place session 2 at the\n"
    "  desired LBA for Dreamcast MIL-CD style layouts.\n\n"
    "Defaults:\n"
    "  target_lba = 45000, sector-size = 2048, overhead = 11400\n\n"
    "Formula (Dreamcast-friendly):\n"
    "  target_size_bytes = (target_lba - overhead_sectors) * sector_size\n"
    "  With defaults: target_size_bytes = (LBA - 11400) * 2048\n\n"
    "Examples:\n"
    "  " << argv0 << " session1.iso             # pads to LBA 45000 → 68,812,800 bytes\n"
    "  " << argv0 << " session1.iso 90000       # pads to LBA 90000 → 160,972,800 bytes\n"
    "  " << argv0 << " session1.iso --dry-run   # just show computed sizes\n";
}

static bool padFileToTargetSize(const std::string& filename, uint64_t targetSizeBytes, bool dryRun) {
    // Get current file size
    uint64_t currentSize = 0;
    try {
        currentSize = fs::file_size(filename);
    } catch (const std::exception& e) {
        std::cerr << "Error: Cannot stat file: " << filename << " (" << e.what() << ")\n";
        return false;
    }

    if (currentSize > targetSizeBytes) {
        std::cerr << "Error: File is already larger than target size. Current = "
                  << currentSize << " bytes, target = " << targetSizeBytes << " bytes.\n";
        return false;
    }

    if (currentSize == targetSizeBytes) {
        std::cout << "No padding needed. File already at target size: " << targetSizeBytes << " bytes.\n";
        return true;
    }

    uint64_t bytesToAdd = targetSizeBytes - currentSize;

    std::cout << "Padding " << filename << "\n"
              << "  Current size : " << currentSize    << " bytes\n"
              << "  Target size  : " << targetSizeBytes << " bytes\n"
              << "  Bytes to add : " << bytesToAdd      << " bytes\n";

    if (dryRun) {
        std::cout << "[dry-run] Skipping write.\n";
        return true;
    }

    std::ofstream out(filename, std::ios::binary | std::ios::app);
    if (!out) {
        std::cerr << "Error: Cannot open file for appending: " << filename << "\n";
        return false;
    }

    const size_t chunk = 8 * 1024 * 1024; // 8 MiB
    std::vector<char> zeros(chunk, 0);

    uint64_t remaining = bytesToAdd;
    while (remaining > 0) {
        size_t toWrite = static_cast<size_t>(std::min<uint64_t>(remaining, zeros.size()));
        out.write(zeros.data(), toWrite);
        if (!out) {
            std::cerr << "Error: Write failed during padding.\n";
            return false;
        }
        remaining -= toWrite;
    }
    out.flush();
    if (!out) {
        std::cerr << "Error: Flush failed after padding.\n";
        return false;
    }

    std::cout << "File padded successfully to " << targetSizeBytes << " bytes.\n";
    return true;
}

static bool computeTargetSize(const Options& opt, uint64_t& outBytes, uint64_t& outDataSectors) {
    if (opt.sectorSize == 0) {
        std::cerr << "Error: sector size cannot be 0.\n";
        return false;
    }
    if (opt.targetLBA <= opt.overheadSectors) {
        std::cerr << "Error: target LBA (" << opt.targetLBA
                  << ") must be greater than overhead sectors (" << opt.overheadSectors << ").\n";
        return false;
    }

    uint64_t dataSectors = opt.targetLBA - opt.overheadSectors;
    uint64_t bytes = dataSectors * static_cast<uint64_t>(opt.sectorSize);

    outBytes = bytes;
    outDataSectors = dataSectors;
    return true;
}

static bool parseArgs(int argc, char* argv[], Options& opt) {
    if (argc < 2) return false;

    opt.filename = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "--dry-run") {
            opt.dryRun = true;
        } else if (a.rfind("--sector-size=", 0) == 0) {
            std::string v = a.substr(std::strlen("--sector-size="));
            if (!parseUint32(v, opt.sectorSize)) {
                std::cerr << "Error: invalid sector size: " << v << "\n";
                return false;
            }
        } else if (a.rfind("--overhead=", 0) == 0) {
            std::string v = a.substr(std::strlen("--overhead="));
            if (!parseUint32(v, opt.overheadSectors)) {
                std::cerr << "Error: invalid overhead sectors: " << v << "\n";
                return false;
            }
        } else {
            // If it's purely numeric, treat as LBA
            uint64_t lba = 0;
            if (!parseUint64(a, lba)) {
                std::cerr << "Error: Unrecognized argument: " << a << "\n";
                return false;
            }
            opt.targetLBA = lba;
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        printUsage(argv[0]);
        return 1;
    }

    uint64_t targetBytes = 0;
    uint64_t dataSectors = 0;
    if (!computeTargetSize(opt, targetBytes, dataSectors)) {
        return 1;
    }

    std::cout << "Dreamcast padding plan:\n"
              << "  Desired Session 2 start LBA : " << opt.targetLBA << "\n"
              << "  Overhead sectors            : " << opt.overheadSectors << " (lead-in + pre-gap + lead-out)\n"
              << "  Data sectors in ISO         : " << dataSectors << "\n"
              << "  Sector size (bytes)         : " << opt.sectorSize << "\n"
              << "  Target ISO size (bytes)     : " << targetBytes << "\n";

    bool ok = padFileToTargetSize(opt.filename, targetBytes, opt.dryRun);
    return ok ? 0 : 1;
}