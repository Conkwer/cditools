#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <cstring>

namespace fs = std::filesystem;

class ISO2RAWConverter {
public:
    struct Options {
        bool mode2 = false;      // Generate MODE2 sectors (default is MODE1)
        bool postgap = false;    // Write 150 sector postgap to output file
        bool scramble = false;   // Scramble sectors
    };

    ISO2RAWConverter(const std::string& inputFile, const std::string& outputFile, const Options& options)
        : inputPath(inputFile), outputPath(outputFile), opts(options) {
        
        if (!fs::exists(inputPath)) {
            throw std::runtime_error("Input file does not exist: " + inputFile);
        }
        
        fileSize = fs::file_size(inputPath);
        if (fileSize % SECTOR_SIZE_ISO != 0) {
            throw std::runtime_error("Input file size is not a multiple of 2048 bytes");
        }
        
        totalSectors = fileSize / SECTOR_SIZE_ISO;
    }

    void convert() {
        std::ifstream input(inputPath, std::ios::binary);
        if (!input.is_open()) {
            throw std::runtime_error("Cannot open input file: " + inputPath.string());
        }

        std::ofstream output(outputPath, std::ios::binary);
        if (!output.is_open()) {
            throw std::runtime_error("Cannot create output file: " + outputPath.string());
        }

        std::cout << "File contains " << (fileSize / (1024 * 1024)) << "MB of data (" 
                  << totalSectors << " blocks)" << std::endl;
        std::cout << "Processing..." << std::endl;

        // Allocate buffers
        std::vector<uint8_t> isoSector(SECTOR_SIZE_ISO);
        std::vector<uint8_t> rawSector(SECTOR_SIZE_RAW);

        uint32_t processedSectors = 0;
        int lastPercentage = -1;

        while (input.read(reinterpret_cast<char*>(isoSector.data()), SECTOR_SIZE_ISO)) {
            // Convert ISO sector to RAW sector
            convertSector(isoSector.data(), rawSector.data(), processedSectors);
            
            // Write the converted sector
            output.write(reinterpret_cast<char*>(rawSector.data()), SECTOR_SIZE_RAW);
            
            processedSectors++;
            
            // Progress reporting
            int percentage = (processedSectors * 100) / totalSectors;
            if (percentage > lastPercentage) {
                std::cout << " " << percentage << "% completed.\r" << std::flush;
                lastPercentage = percentage;
            }
        }

        // Add postgap if requested
        if (opts.postgap) {
            std::cout << "Writing POSTGAP (150 blocks)...";
            std::vector<uint8_t> postgapSector(SECTOR_SIZE_RAW, 0);
            
            for (int i = 0; i < 150; i++) {
                output.write(reinterpret_cast<char*>(postgapSector.data()), SECTOR_SIZE_RAW);
            }
            std::cout << " done." << std::endl;
        }

        std::cout << "Conversion completed!" << std::endl;
    }

private:
    static constexpr uint32_t SECTOR_SIZE_ISO = 2048;
    static constexpr uint32_t SECTOR_SIZE_RAW = 2352;
    
    fs::path inputPath;
    fs::path outputPath;
    Options opts;
    uintmax_t fileSize = 0;
    uint32_t totalSectors = 0;

    void convertSector(const uint8_t* isoSector, uint8_t* rawSector, uint32_t sectorNumber) {
        // Sync bytes (12 bytes)
        std::memset(rawSector, 0, 12);
        rawSector[0] = 0x00;
        rawSector[1] = 0xFF;
        rawSector[2] = 0xFF;
        rawSector[3] = 0xFF;
        rawSector[4] = 0xFF;
        rawSector[5] = 0xFF;
        rawSector[6] = 0xFF;
        rawSector[7] = 0xFF;
        rawSector[8] = 0xFF;
        rawSector[9] = 0xFF;
        rawSector[10] = 0xFF;
        rawSector[11] = 0x00;

        // Header (4 bytes: minute, second, frame, mode)
        rawSector[12] = (sectorNumber / (75 * 60)) % 60;  // Minute
        rawSector[13] = (sectorNumber / 75) % 60;         // Second
        rawSector[14] = sectorNumber % 75;                // Frame
        rawSector[15] = opts.mode2 ? 2 : 1;               // Mode

        // Subheader (8 bytes for Mode 2 Form 1/2)
        if (opts.mode2) {
            std::memset(rawSector + 16, 0, 8);
            // File number, channel, submode, coding info
            // Simplified implementation - real implementation would need more details
            rawSector[16] = 1;  // File number
            rawSector[18] = 0x20;  // Form 1 (data)
        }

        // Copy user data
        uint8_t* dataStart = opts.mode2 ? rawSector + 24 : rawSector + 16;
        std::memcpy(dataStart, isoSector, SECTOR_SIZE_ISO);

        // EDC/ECC calculation would go here for Mode 2 Form 1
        if (opts.mode2) {
            // Placeholder for EDC calculation
            // calculateEDC(rawSector + 16, 2068, rawSector + 2072);
            
            // Placeholder for ECC calculation
            // calculateECC(rawSector + 12, rawSector + 2072 + 4);
        }

        // Scramble if requested
        if (opts.scramble) {
            scrambleSector(rawSector + 12, 2340);  // Scramble from header onward
        }
    }

    void scrambleSector(uint8_t* sectorData, size_t length) {
        // Simple scrambling algorithm (actual CD-ROM scrambling is more complex)
        // This is a placeholder - real implementation would use proper CD-ROM scrambling
        static const uint8_t scrambleKey[10] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44};
        
        for (size_t i = 0; i < length; i++) {
            sectorData[i] ^= scrambleKey[i % 10];
        }
    }
};

void printUsage() {
    std::cout << "ISO2RAW.EXE - Modern Version" << std::endl;
    std::cout << "Copyright (c) 2023" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: iso2raw <isofile> <rawfile> [options]" << std::endl;
    std::cout << "isofile   - ISO9660 input file (2048 byte sectors)" << std::endl;
    std::cout << "rawfile   - RAW output file (2352 byte sectors)" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --mode2     - Generate MODE2 sectors (default is MODE1)" << std::endl;
    std::cout << "  --postgap   - Write 150 sector postgap to output file" << std::endl;
    std::cout << "  --scramble  - Scramble sectors" << std::endl;
    std::cout << "  --help      - Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    // Parse command line arguments
    std::string inputFile;
    std::string outputFile;
    ISO2RAWConverter::Options options;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "--mode2") {
            options.mode2 = true;
        } else if (arg == "--postgap") {
            options.postgap = true;
        } else if (arg == "--scramble") {
            options.scramble = true;
        } else if (inputFile.empty()) {
            inputFile = arg;
        } else if (outputFile.empty()) {
            outputFile = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return 1;
        }
    }

    if (inputFile.empty() || outputFile.empty()) {
        std::cerr << "Error: Input and output filenames must be specified!" << std::endl;
        printUsage();
        return 1;
    }

    try {
        ISO2RAWConverter converter(inputFile, outputFile, options);
        converter.convert();
		std::cout << "Nice!" << std::endl;
        // std::cout << "Conversion completed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}