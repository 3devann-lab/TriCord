#include "network/media_cache.h"
#include "core/config.h"
#include "core/log.h"
#include "utils/file_utils.h"
#include "utils/worker_thread.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <vector>

namespace Network {
namespace MediaCache {

namespace {

const std::string MEDIA_ROOT = std::string(CONFIG_DIR_PATH) + "/cache/media";
const std::string MESSAGE_ROOT = std::string(CONFIG_DIR_PATH) + "/cache/messages";
const uint64_t MAX_BYTES = 48ULL * 1024 * 1024;
const uint64_t TRIM_TO_BYTES = 32ULL * 1024 * 1024;
const size_t MAX_ENTRY_BYTES = 2 * 1024 * 1024;
const size_t MAX_MESSAGE_FILES = 80;
const char HEX[] = "0123456789abcdef";

std::mutex jobMutex;
std::condition_variable jobReady;
std::deque<std::function<void()>> jobs;
bool stopping = false;
std::atomic<bool> running{false};
std::atomic<bool> trimQueued{false};
Utils::WorkerThread worker;
std::atomic<uint64_t> totalBytes{0};

// Signed attachment links change their ex/is/hm parameters whenever they are re-issued,
// but point at the same file.
std::string normalisedUrl(const std::string &url) {
	size_t q = url.find('?');
	if (q == std::string::npos) {
		return url;
	}
	std::string out = url.substr(0, q);
	char sep = '?';
	size_t pos = q + 1;
	while (pos <= url.size()) {
		size_t amp = url.find('&', pos);
		if (amp == std::string::npos) {
			amp = url.size();
		}
		std::string param = url.substr(pos, amp - pos);
		bool signature = param.compare(0, 3, "ex=") == 0 || param.compare(0, 3, "is=") == 0 ||
		                 param.compare(0, 3, "hm=") == 0;
		if (!param.empty() && !signature) {
			out += sep;
			out += param;
			sep = '&';
		}
		pos = amp + 1;
	}
	return out;
}

std::string keyFor(const std::string &url) {
	uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : normalisedUrl(url)) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	std::string key(16, '0');
	for (int i = 15; i >= 0; i--) {
		key[i] = HEX[hash & 0xF];
		hash >>= 4;
	}
	return key;
}

std::string pathFor(const std::string &key) { return MEDIA_ROOT + "/" + key[0] + "/" + key + ".bin"; }

bool readAll(const std::string &path, std::string &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	bool ok = false;
	if (size > 0 && (size_t)size <= MAX_ENTRY_BYTES) {
		out.resize((size_t)size);
		ok = fread(&out[0], 1, (size_t)size, f) == (size_t)size;
	}
	fclose(f);
	if (!ok) {
		out.clear();
	}
	return ok;
}

// Written beside the target and renamed, so a power cut never leaves half a file.
bool writeAtomic(const std::string &path, const std::string &data) {
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) {
		return false;
	}
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		remove(tmp.c_str());
		return false;
	}
	remove(path.c_str());
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		remove(tmp.c_str());
		return false;
	}
	return true;
}

struct Entry {
	std::string path;
	uint64_t size;
	time_t mtime;
};

void listFiles(const std::string &dir, std::vector<Entry> &out) {
	DIR *d = opendir(dir.c_str());
	if (!d) {
		return;
	}
	while (struct dirent *e = readdir(d)) {
		if (e->d_name[0] == '.') {
			continue;
		}
		std::string path = dir + "/" + e->d_name;
		struct stat st;
		if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
			out.push_back({path, (uint64_t)st.st_size, st.st_mtime});
		}
	}
	closedir(d);
}

void listDirectories(const std::string &dir, std::vector<std::string> &out) {
	DIR *d = opendir(dir.c_str());
	if (!d) {
		return;
	}
	while (struct dirent *e = readdir(d)) {
		if (e->d_name[0] == '.') {
			continue;
		}
		std::string path = dir + "/" + e->d_name;
		struct stat st;
		if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
			out.push_back(path);
		}
	}
	closedir(d);
}

void deleteOldest(std::vector<Entry> &entries, uint64_t &bytes, uint64_t target, size_t keepCount) {
	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.mtime < b.mtime; });
	size_t remaining = entries.size();
	for (const Entry &e : entries) {
		if (bytes <= target && remaining <= keepCount) {
			break;
		}
		if (remove(e.path.c_str()) == 0) {
			bytes -= std::min(bytes, e.size);
			remaining--;
		}
	}
}

void scanAndTrim() {
	trimQueued.store(false);
	std::vector<Entry> media;
	for (int i = 0; i < 16; i++) {
		listFiles(MEDIA_ROOT + "/" + HEX[i], media);
	}
	uint64_t mediaBytes = 0;
	for (const Entry &e : media) {
		mediaBytes += e.size;
	}
	if (mediaBytes > MAX_BYTES) {
		uint64_t before = mediaBytes;
		deleteOldest(media, mediaBytes, TRIM_TO_BYTES, SIZE_MAX);
		Logger::log("[Cache] Trimmed images from %llu to %llu bytes", (unsigned long long)before,
		            (unsigned long long)mediaBytes);
	}

	uint64_t messageBytes = 0;
	std::vector<std::string> accounts;
	listDirectories(MESSAGE_ROOT, accounts);
	for (const std::string &dir : accounts) {
		std::vector<Entry> files;
		listFiles(dir, files);
		uint64_t bytes = 0;
		for (const Entry &e : files) {
			bytes += e.size;
		}
		if (files.size() > MAX_MESSAGE_FILES) {
			deleteOldest(files, bytes, 0, MAX_MESSAGE_FILES);
		}
		messageBytes += bytes;
	}
	totalBytes.store(mediaBytes + messageBytes);
}

void queueTrim() {
	if (!trimQueued.exchange(true)) {
		runJob(scanAndTrim);
	}
}

void workerLoop() {
	while (true) {
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(jobMutex);
			jobReady.wait(lock, [] { return stopping || !jobs.empty(); });
			if (stopping && jobs.empty()) {
				return;
			}
			job = std::move(jobs.front());
			jobs.pop_front();
		}
		job();
	}
}

void deleteTree(const std::string &dir) {
	std::vector<Entry> files;
	listFiles(dir, files);
	for (const Entry &e : files) {
		remove(e.path.c_str());
	}
	std::vector<std::string> dirs;
	listDirectories(dir, dirs);
	for (const std::string &sub : dirs) {
		deleteTree(sub);
		rmdir(sub.c_str());
	}
}

void ensureLayout() {
	for (int i = 0; i < 16; i++) {
		Utils::File::ensureDirectory(MEDIA_ROOT + "/" + HEX[i]);
	}
	Utils::File::ensureDirectory(MESSAGE_ROOT);
}

} // namespace

void init() {
	if (running.load()) {
		return;
	}
	ensureLayout();
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		stopping = false;
	}
	worker.start(workerLoop, 2, 32 * 1024);
	running.store(true);
	queueTrim();
}

void shutdown() {
	if (!running.exchange(false)) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		stopping = true;
		jobs.clear();
	}
	jobReady.notify_all();
	if (worker.joinable()) {
		worker.join();
	}
}

void runJob(std::function<void()> job) {
	if (!running.load()) {
		job();
		return;
	}
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		jobs.push_back(std::move(job));
	}
	jobReady.notify_one();
}

void fetch(const std::string &url, RequestPriority priority, ResponseCallback callback,
           std::map<std::string, std::string> headers) {
	if (!running.load() || !Config::getInstance().isImageCacheEnabled()) {
		NetworkManager::getInstance().enqueue(url, "GET", "", priority, std::move(callback), std::move(headers));
		return;
	}

	runJob([url, priority, callback = std::move(callback), headers = std::move(headers)]() mutable {
		std::string path = pathFor(keyFor(url));
		HttpResponse cached;
		if (readAll(path, cached.body)) {
			cached.success = true;
			cached.statusCode = 200;
			callback(cached);
			return;
		}
		NetworkManager::getInstance().enqueue(
		    url, "GET", "", priority,
		    [path, callback](HttpResponse &resp) {
			    if (resp.success && resp.statusCode == 200 && !resp.body.empty() &&
			        resp.body.size() <= MAX_ENTRY_BYTES && Config::getInstance().isImageCacheEnabled()) {
				    if (writeAtomic(path, resp.body)) {
					    uint64_t now = totalBytes.fetch_add(resp.body.size()) + resp.body.size();
					    if (now > MAX_BYTES) {
						    queueTrim();
					    }
				    }
			    }
			    callback(resp);
		    },
		    headers);
	});
}

void clearAll() {
	runJob([] {
		deleteTree(MEDIA_ROOT);
		deleteTree(MESSAGE_ROOT);
		ensureLayout();
		totalBytes.store(0);
		Logger::log("[Cache] Cleared");
	});
}

uint64_t sizeBytes() { return totalBytes.load(); }

std::string messagesPath(const std::string &userId, const std::string &channelId) {
	return MESSAGE_ROOT + "/" + userId + "/" + channelId + ".json";
}

void storeMessages(const std::string &userId, const std::string &channelId, const std::string &json) {
	if (userId.empty() || channelId.empty() || !Config::getInstance().isMessageCacheEnabled()) {
		return;
	}
	runJob([userId, channelId, json] {
		std::string path = messagesPath(userId, channelId);
		Utils::File::ensureDirectory(MESSAGE_ROOT + "/" + userId);
		bool existed = Utils::File::exists(path);
		if (writeAtomic(path, json) && !existed) {
			totalBytes.fetch_add(json.size());
			static int newFiles = 0;
			if (++newFiles % 20 == 0) {
				queueTrim();
			}
		}
	});
}

} // namespace MediaCache
} // namespace Network
