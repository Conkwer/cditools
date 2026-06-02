#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

class FileCreator {
public:
    static void createEmptyFile(const std::string& filename, std::size_t size) {
        if (size == 0) {
            throw std::invalid_argument("File size must be greater than 0");
        }

        fs::path filePath(filename);
        
        // Check if directory exists or create it
        if (filePath.has_parent_path()) {
            fs::create_directories(filePath.parent_path());
        }

        // Create file with specified size
        std::ofstream file(filePath, std::ios::binary | std::ios::out);
        if (!file) {
            throw std::runtime_error("Failed to create file: " + filename);
        }

        // Seek to desired size and write a single byte at the end
        file.seekp(size - 1);
        file.write("", 1);
        
        std::cout << "Created file: " << filename << " (" << size << " bytes)\n";
    }
};

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [Size in bytes] [Filename]\n"
              << "Example: " << programName << " 307200 Dummy.bin\n"
              << "Creates an empty file of the specified size\n";
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }

    try {
        // Parse size
        std::size_t size = std::stoull(argv[1]);
        std::string filename = argv[2];

        FileCreator::createEmptyFile(filename, size);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}