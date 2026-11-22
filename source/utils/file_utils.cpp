#include "utils/file_utils.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>
#include <vector>

namespace Utils {
namespace File {

std::vector<char> readFile(const std::string &path) {
	FILE *fp = fopen(path.c_str(), "r");
	if (!fp) {
		return {};
	}

	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	std::vector<char> buffer(size + 1);
	if (size > 0) {
		fread(buffer.data(), 1, size, fp);
	}
	buffer[size] = '\0';
	fclose(fp);
	return buffer;
}

std::vector<unsigned char> readFileBinary(const std::string &path) {
	FILE *fp = fopen(path.c_str(), "rb");
	if (!fp) {
		return {};
	}

	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	std::vector<unsigned char> buffer(size);
	if (size > 0) {
		fread(buffer.data(), 1, size, fp);
	}
	fclose(fp);
	return buffer;
}

bool writeFile(const std::string &path, const std::vector<unsigned char> &data) {
	FILE *fp = fopen(path.c_str(), "wb");
	if (!fp) {
		return false;
	}
	fwrite(data.data(), 1, data.size(), fp);
	fclose(fp);
	return true;
}

bool writeFile(const std::string &path, const std::string &data) {
	FILE *fp = fopen(path.c_str(), "w");
	if (!fp) {
		return false;
	}
	fputs(data.c_str(), fp);
	fclose(fp);
	return true;
}

bool exists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

bool ensureDirectory(const std::string &path) {
	if (path.empty()) {
		return false;
	}
	// Skip the "sdmc:/" device prefix, which cannot be created.
	size_t start = path.find(":/");
	start = start == std::string::npos ? 1 : start + 2;
	for (size_t pos = path.find('/', start); ; pos = path.find('/', pos + 1)) {
		const std::string part = path.substr(0, pos);
		if (!part.empty() && part.back() != ':' && !exists(part)) {
			mkdir(part.c_str(), 0777);
		}
		if (pos == std::string::npos) {
			break;
		}
	}
	return exists(path);
}

long long fileSize(const std::string &path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		return -1;
	}
	return (long long)st.st_size;
}

bool copyFile(const std::string &from, const std::string &to) {
	FILE *in = fopen(from.c_str(), "rb");
	if (!in) {
		return false;
	}
	FILE *out = fopen(to.c_str(), "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	std::vector<char> buffer(64 * 1024);
	bool ok = true;
	size_t got = 0;
	while ((got = fread(buffer.data(), 1, buffer.size(), in)) > 0) {
		if (fwrite(buffer.data(), 1, got, out) != got) {
			ok = false;
			break;
		}
	}
	fclose(in);
	if (fclose(out) != 0) {
		ok = false;
	}
	if (!ok) {
		remove(to.c_str());
	}
	return ok;
}

std::string baseName(const std::string &path) {
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string parentDirectory(const std::string &path) {
	std::string trimmed = path;
	while (trimmed.size() > 1 && trimmed.back() == '/') {
		trimmed.pop_back();
	}
	const size_t slash = trimmed.find_last_of('/');
	if (slash == std::string::npos) {
		return trimmed;
	}
	// Keep the root's slash, so "sdmc:/DCIM" goes to "sdmc:/".
	if (slash > 0 && trimmed[slash - 1] == ':') {
		return trimmed.substr(0, slash + 1);
	}
	return trimmed.substr(0, slash);
}

std::string extension(const std::string &path) {
	const std::string name = baseName(path);
	const size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot + 1 >= name.size()) {
		return "";
	}
	std::string ext = name.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return ext;
}

std::string mimeType(const std::string &path) {
	const std::string ext = extension(path);
	if (ext == "jpg" || ext == "jpeg") {
		return "image/jpeg";
	}
	if (ext == "png") {
		return "image/png";
	}
	if (ext == "gif") {
		return "image/gif";
	}
	if (ext == "webp") {
		return "image/webp";
	}
	if (ext == "bmp") {
		return "image/bmp";
	}
	if (ext == "mp4" || ext == "m4v") {
		return "video/mp4";
	}
	if (ext == "mov") {
		return "video/quicktime";
	}
	if (ext == "webm") {
		return "video/webm";
	}
	if (ext == "mkv") {
		return "video/x-matroska";
	}
	if (ext == "avi") {
		return "video/x-msvideo";
	}
	if (ext == "3gp") {
		return "video/3gpp";
	}
	if (ext == "mp3") {
		return "audio/mpeg";
	}
	if (ext == "ogg") {
		return "audio/ogg";
	}
	if (ext == "wav") {
		return "audio/wav";
	}
	if (ext == "m4a") {
		return "audio/mp4";
	}
	if (ext == "txt" || ext == "log" || ext == "json" || ext == "ini" || ext == "cfg") {
		return "text/plain";
	}
	return "application/octet-stream";
}

bool isImagePath(const std::string &path) {
	const std::string ext = extension(path);
	return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp" || ext == "bmp" ||
	       ext == "mpo";
}

bool isVideoPath(const std::string &path) {
	const std::string ext = extension(path);
	return ext == "mp4" || ext == "m4v" || ext == "mov" || ext == "webm" || ext == "mkv" || ext == "3gp";
}

std::string sanitizeFileName(const std::string &name) {
	std::string out;
	out.reserve(name.size());
	for (char c : name) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
		    c == '|' || (unsigned char)c < 0x20) {
			out += '_';
		} else {
			out += c;
		}
	}
	if (out.empty() || out == "." || out == "..") {
		out = "file";
	}
	return out;
}

std::string formatSize(long long bytes) {
	char buffer[32];
	if (bytes < 1024) {
		snprintf(buffer, sizeof(buffer), "%lld B", bytes);
	} else if (bytes < 1024 * 1024) {
		snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0);
	} else if (bytes < 1024LL * 1024 * 1024) {
		snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
	} else {
		snprintf(buffer, sizeof(buffer), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
	}
	return buffer;
}

std::string uniquePath(const std::string &dir, const std::string &name) {
	const std::string clean = sanitizeFileName(name);
	std::string base = clean;
	std::string ext;
	const size_t dot = clean.find_last_of('.');
	if (dot != std::string::npos && dot > 0) {
		base = clean.substr(0, dot);
		ext = clean.substr(dot);
	}
	std::string candidate = dir + "/" + clean;
	for (int i = 1; exists(candidate) && i < 1000; i++) {
		candidate = dir + "/" + base + " (" + std::to_string(i) + ")" + ext;
	}
	return candidate;
}

} // namespace File
} // namespace Utils
