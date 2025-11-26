#include "sha256.h" // Include the provided SHA256 implementation
#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <vector>
#include <algorithm>
#include <utility>
#include <iomanip> // For formatting output

namespace fs = std::filesystem;

// Type alias for easier reading
using uint64_t = std::uintmax_t;

/**
 * @brief Converts bytes to a human-readable string (KB, MB, GB).
 */
std::string formatSize(uint64_t bytes) {
    const uint64_t KB = 1024;
    const uint64_t MB = 1024 * KB;
    const uint64_t GB = 1024 * MB;

    if (bytes >= GB) {
        return std::to_string(static_cast<double>(bytes) / GB).substr(0, 4) + " GB";
    }
    if (bytes >= MB) {
        return std::to_string(static_cast<double>(bytes) / MB).substr(0, 4) + " MB";
    }
    if (bytes >= KB) {
        return std::to_string(static_cast<double>(bytes) / KB).substr(0, 4) + " KB";
    }
    return std::to_string(bytes) + " Bytes";
}


/**
 * @brief Calculates the SHA-256 hash of a given file.
 *
 * @param path The path to the file.
 * @return std::string The hexadecimal SHA-256 hash, or an empty string on error.
 */
std::string calculateFileHash(const fs::path& path) {
    // Open the file in binary mode
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    SHA256 sha256;
    char buffer[131072]; // 128KB buffer
    
    // Read file content in chunks and update the hash object
    while (file.read(buffer, sizeof(buffer))) {
        sha256.add(buffer, file.gcount());
    }
    // Add the final partial chunk (if any)
    if (file.gcount() > 0) {
        sha256.add(buffer, file.gcount());
    }

    if (file.bad()) {
        return "";
    }

    return sha256.getHash();
}

/**
 * @brief Main logic for scanning the directory, listing all duplicates, and handling batch deletion.
 *
 * @param rootDir The path to the directory to scan.
 */
void findAndRemoveDuplicates(const fs::path& rootDir) {
    // A map to store: Hash -> Vector of file paths with that hash
    std::map<std::string, std::vector<fs::path>> hashToPaths;
    
    // Vector to store all files identified for deletion
    std::vector<fs::path> filesToDelete; 
    
    // Accumulator for the total size of files to delete
    uint64_t totalDuplicateSize = 0; 
    
    std::cout << "Starting scan of directory: " << rootDir.string() << "\n";
    std::cout << "This may take a while for large directories..." << "\n";

    // --- PHASE 1: Scan and Hash with Live Progress ---
    try {
        for (const auto& entry : fs::recursive_directory_iterator(rootDir)) {
            if (entry.is_regular_file()) {
                const fs::path& filePath = entry.path();
                
                // Progress output (overwrites previous line)
                std::cout << "Calculating hash for: " << filePath.string() << "\r" << std::flush;

                std::string hash = calculateFileHash(filePath);

                if (!hash.empty()) {
                    hashToPaths[hash].push_back(filePath);
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "\nFilesystem Error during scan: " << e.what() << "\n";
        return;
    }
    
    std::cout << "\n"; // Clear the progress line

    std::cout << "Scan Complete. Checking for duplicates..." << "\n";

    // --- PHASE 2: Identify, List, and Calculate Duplicates ---
    
    bool foundDuplicates = false; 
    
    // Iterate through the map to find hashes with more than one file path
    for (const auto& pair : hashToPaths) {
        const std::vector<fs::path>& paths = pair.second;

        if (paths.size() > 1) {
            foundDuplicates = true;
            
            const fs::path& keeper = paths[0];
            
            std::cout << "\n--- Duplicate Group (Keeper: " << keeper.string() << ") ---" << "\n";
            
            // Collect and calculate size for all duplicates (starting from index 1)
            for (size_t i = 1; i < paths.size(); ++i) {
                const fs::path& dupPath = paths[i];
                filesToDelete.push_back(dupPath);
                
                try {
                    uint64_t size = fs::file_size(dupPath);
                    totalDuplicateSize += size;
                    std::cout << "   -> Duplicate: " << dupPath.string() << " (" << formatSize(size) << ")" << "\n";
                } catch (const fs::filesystem_error& e) {
                    std::cerr << "   -> Warning: Could not get size for " << dupPath.string() << "\n";
                }
            }
        }
    }

    if (!foundDuplicates) {
        std::cout << "\nNo duplicate files found in the directory." << "\n";
        return;
    }
    
    // --- PHASE 3: Batch Prompt and Deletion ---
    
    std::cout << "\n========================================================" << "\n";
    std::cout << "   Summary: " << filesToDelete.size() << " duplicate file(s) identified." << "\n";
    std::cout << "   Total Size to Reclaim: " << formatSize(totalDuplicateSize) << "\n";
    std::cout << "   Do you want to **delete ALL** " << filesToDelete.size() << " files listed above (Y/N)? ";
    std::cout << "\n========================================================" << "\n";
    
    std::string response;
    std::cin >> response;

    if (response.size() == 1 && (response[0] == 'Y' || response[0] == 'y')) {
        int deleteCount = 0;
        std::cout << "\nStarting batch deletion..." << "\n";
        for (const auto& dupPath : filesToDelete) {
            try {
                fs::remove(dupPath);
                std::cout << "   [OK] Deleted: " << dupPath.string() << "\n";
                deleteCount++;
            } catch (const fs::filesystem_error& e) {
                std::cerr << "   [FAIL] Error deleting file " << dupPath.string() << ": " << e.what() << "\n";
            }
        }
        std::cout << "\n**Batch Operation Complete:** " << deleteCount << " files successfully deleted." << "\n";
    } else {
        std::cout << "\nDeletion skipped for all " << filesToDelete.size() << " identified files." << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <directory_path>" << "\n";
        return 1;
    }

    fs::path rootDir = argv[1];

    if (!fs::exists(rootDir)) {
        std::cerr << "Error: Directory does not exist: " << rootDir.string() << "\n";
        return 1;
    }
    if (!fs::is_directory(rootDir)) {
        std::cerr << "Error: Path is not a directory: " << rootDir.string() << "\n";
        return 1;
    }

    findAndRemoveDuplicates(rootDir);

    return 0;
}
