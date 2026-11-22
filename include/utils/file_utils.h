#pragma once
#include <string>
#include <vector>

namespace Utils {
namespace File {

std::vector<char> readFile(const std::string &path);
std::vector<unsigned char> readFileBinary(const std::string &path);
bool writeFile(const std::string &path, const std::vector<unsigned char> &data);
bool writeFile(const std::string &path, const std::string &data);

// Creates every missing directory along the path.
bool ensureDirectory(const std::string &path);
bool exists(const std::string &path);
// -1 when the file cannot be read.
long long fileSize(const std::string &path);
bool copyFile(const std::string &from, const std::string &to);

std::string baseName(const std::string &path);
std::string parentDirectory(const std::string &path);
// Lower case and without the dot, e.g. "jpg".
std::string extension(const std::string &path);
std::string mimeType(const std::string &path);
bool isImagePath(const std::string &path);
bool isVideoPath(const std::string &path);
// Swaps characters FAT cannot store for underscores.
std::string sanitizeFileName(const std::string &name);
// "1.4 MB" style.
std::string formatSize(long long bytes);
// A path in dir that does not exist yet, built from name.
std::string uniquePath(const std::string &dir, const std::string &name);

} // namespace File
} // namespace Utils
