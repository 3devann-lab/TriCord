#include "core/updater.h"
#include "core/config.h"
#include "core/log.h"
#include "network/network_manager.h"
#include "utils/file_utils.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include "utils/worker_thread.h"
#include <3ds.h>
#include <algorithm>
#include <map>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <rapidjson/document.h>

namespace Core {
namespace Updater {

namespace {

const char *DEFAULT_REPO = "3devann-lab/TriCord";
const char *BRANCH = "main";

std::mutex mutex;
Status current;
std::shared_ptr<Network::TransferProgress> progress;
Utils::WorkerThread installer;
std::atomic<int64_t> installDone{0};
std::atomic<int64_t> installTotal{0};

std::string updatePath() { return std::string(CONFIG_DIR_PATH) + "/update/TriCord.cia"; }

void setState(State state, const std::string &error = "") {
	{
		std::lock_guard<std::mutex> lock(mutex);
		current.state = state;
		current.error = error;
	}
	Utils::requestRedraw();
}

std::map<std::string, std::string> githubHeaders() {
	return {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
}

// "Update TriCord.cia for abc1234 [skip ci]" -> "abc1234".
std::string shaFromMessage(const std::string &message) {
	const std::string marker = "TriCord.cia for ";
	size_t pos = message.find(marker);
	if (pos == std::string::npos) {
		return "";
	}
	pos += marker.size();
	size_t end = pos;
	while (end < message.size() && isxdigit((unsigned char)message[end])) {
		end++;
	}
	return message.substr(pos, end - pos);
}

std::string firstLine(const std::string &text) { return text.substr(0, text.find('\n')); }

// A merge's first line says only which branch; the pull request's title follows it.
std::string changeTitle(const std::string &message) {
	std::string line = firstLine(message);
	if (line.rfind("Merge pull request", 0) == 0 || line.rfind("Merge branch", 0) == 0) {
		size_t blank = message.find("\n\n");
		if (blank != std::string::npos) {
			std::string rest = firstLine(message.substr(blank + 2));
			if (!rest.empty()) {
				return rest;
			}
		}
	}
	return line;
}

void fetchChanges(time_t since) {
	std::string url = "https://api.github.com/repos/" + repository() + "/commits?sha=" + BRANCH + "&per_page=30";
	if (since > 0) {
		url += "&since=" + UI::MessageUtils::getISOTimestamp(since + 1);
	}
	Network::NetworkManager::getInstance().enqueue(
	    url, "GET", "", Network::RequestPriority::BACKGROUND,
	    [](Network::HttpResponse &resp) {
		    rapidjson::Document doc;
		    doc.Parse(resp.body.c_str());
		    if (resp.statusCode != 200 || doc.HasParseError() || !doc.IsArray()) {
			    return;
		    }
		    std::vector<std::string> changes;
		    for (const auto &entry : doc.GetArray()) {
			    if (!entry.IsObject() || !entry.HasMember("commit") || !entry["commit"].IsObject()) {
				    continue;
			    }
			    const auto &commit = entry["commit"];
			    if (!commit.HasMember("message") || !commit["message"].IsString()) {
				    continue;
			    }
			    std::string message = commit["message"].GetString();
			    if (message.find("[skip ci]") != std::string::npos) {
				    continue;
			    }
			    std::string title = changeTitle(message);
			    // A merge and the commits it brings in often repeat each other.
			    if (!title.empty() && std::find(changes.begin(), changes.end(), title) == changes.end()) {
				    changes.push_back(title);
			    }
			    if (changes.size() >= 12) {
				    break;
			    }
		    }
		    std::lock_guard<std::mutex> lock(mutex);
		    current.changes = changes;
		    Utils::requestRedraw();
	    },
	    githubHeaders());
}

void installWorker() {
	const std::string path = updatePath();
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		setState(State::FAILED, "open");
		return;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	// A CIA starts with its 0x2020-byte header size; anything else is an error page.
	uint32_t header = 0;
	if (size < 1024 * 1024 || fread(&header, 1, 4, f) != 4 || header != 0x2020) {
		fclose(f);
		remove(path.c_str());
		setState(State::FAILED, "corrupt");
		return;
	}
	fseek(f, 0, SEEK_SET);
	installTotal = size;
	installDone = 0;

	// Fail rather than wait forever if the install service is busy.
	srvSetBlockingPolicy(true);
	Result amRes = amInit();
	srvSetBlockingPolicy(false);
	if (R_FAILED(amRes)) {
		Logger::log("[Updater] amInit failed: %08lX", (unsigned long)amRes);
		fclose(f);
		setState(State::FAILED, "am");
		return;
	}
	Handle handle = 0;
	Result res = AM_StartCiaInstall(MEDIATYPE_SD, &handle);
	if (R_FAILED(res)) {
		Logger::log("[Updater] AM_StartCiaInstall failed: %08lX", (unsigned long)res);
		amExit();
		fclose(f);
		setState(State::FAILED, "am");
		return;
	}

	const size_t CHUNK = 128 * 1024;
	std::vector<uint8_t> buffer(CHUNK);
	u64 offset = 0;
	bool ok = true;
	while (ok) {
		size_t got = fread(buffer.data(), 1, CHUNK, f);
		if (got == 0) {
			break;
		}
		u32 written = 0;
		res = FSFILE_Write(handle, &written, offset, buffer.data(), (u32)got, 0);
		if (R_FAILED(res) || written != got) {
			Logger::log("[Updater] FSFILE_Write failed at %llu: %08lX", (unsigned long long)offset, (unsigned long)res);
			ok = false;
			break;
		}
		offset += got;
		installDone = (int64_t)offset;
		Utils::requestRedraw();
	}
	fclose(f);

	if (ok && offset == (u64)size) {
		res = AM_FinishCiaInstall(handle);
		ok = R_SUCCEEDED(res);
		if (!ok) {
			Logger::log("[Updater] AM_FinishCiaInstall failed: %08lX", (unsigned long)res);
		}
	} else {
		AM_CancelCIAInstall(handle);
		ok = false;
	}
	amExit();

	if (ok) {
		remove(path.c_str());
		Logger::log("[Updater] Installed %ld bytes", size);
		setState(State::INSTALLED);
	} else {
		setState(State::FAILED, "install");
	}
}

} // namespace

const BuildInfo &build() {
	static BuildInfo info;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		std::vector<char> raw = Utils::File::readFile("romfs:/build_info.json");
		rapidjson::Document doc;
		if (!raw.empty()) {
			doc.Parse(raw.data());
		}
		if (!raw.empty() && !doc.HasParseError() && doc.IsObject()) {
			if (doc.HasMember("sha") && doc["sha"].IsString()) {
				info.sha = doc["sha"].GetString();
			}
			if (doc.HasMember("time") && doc["time"].IsInt64()) {
				info.time = (time_t)doc["time"].GetInt64();
			}
			if (doc.HasMember("repo") && doc["repo"].IsString()) {
				info.repo = doc["repo"].GetString();
			}
		}
	}
	return info;
}

std::string repository() { return build().repo.empty() ? DEFAULT_REPO : build().repo; }

Status status() {
	std::lock_guard<std::mutex> lock(mutex);
	Status copy = current;
	if (copy.state == State::DOWNLOADING && progress) {
		copy.done = progress->done.load();
		copy.total = progress->total.load();
	} else if (copy.state == State::INSTALLING) {
		copy.done = installDone.load();
		copy.total = installTotal.load();
	}
	return copy;
}

void checkAsync() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (current.state == State::CHECKING || current.state == State::DOWNLOADING ||
		    current.state == State::INSTALLING || current.state == State::INSTALLED) {
			return;
		}
		current = Status();
		current.state = State::CHECKING;
	}
	std::string url = "https://api.github.com/repos/" + repository() + "/commits?sha=" + BRANCH +
	                  "&path=TriCord.cia&per_page=1";
	Network::NetworkManager::getInstance().enqueue(
	    url, "GET", "", Network::RequestPriority::BACKGROUND,
	    [](Network::HttpResponse &resp) {
		    rapidjson::Document doc;
		    doc.Parse(resp.body.c_str());
		    if (resp.statusCode != 200 || doc.HasParseError() || !doc.IsArray() || doc.Empty() ||
		        !doc[0].IsObject() || !doc[0].HasMember("commit")) {
			    Logger::log("[Updater] Check failed: %ld", resp.statusCode);
			    setState(State::FAILED, resp.statusCode == 403 ? "rate" : "check");
			    return;
		    }
		    const auto &commit = doc[0]["commit"];
		    std::string message = commit.HasMember("message") && commit["message"].IsString() ? commit["message"].GetString() : "";
		    std::string date;
		    if (commit.HasMember("committer") && commit["committer"].IsObject() && commit["committer"].HasMember("date") &&
		        commit["committer"]["date"].IsString()) {
			    date = commit["committer"]["date"].GetString();
		    }
		    std::string latest = shaFromMessage(message);
		    time_t latestTime = UI::MessageUtils::parseISO8601(date);

		    const BuildInfo &mine = build();
		    bool same = !latest.empty() && !mine.sha.empty() && mine.sha.rfind(latest, 0) == 0;
		    // A build newer than main's (from a branch, say) is not behind it.
		    bool newer = mine.time > 0 && latestTime > 0 && latestTime <= mine.time;
		    {
			    std::lock_guard<std::mutex> lock(mutex);
			    current.latestSha = latest;
			    current.latestTime = latestTime;
			    current.state = (same || newer) ? State::UP_TO_DATE : State::AVAILABLE;
		    }
		    Logger::log("[Updater] Latest %s, this build %s: %s", latest.c_str(), mine.sha.c_str(),
		                (same || newer) ? "up to date" : "update available");
		    if (!same && !newer) {
			    fetchChanges(mine.time);
		    }
		    Utils::requestRedraw();
	    },
	    githubHeaders());
}

void installAsync() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (current.state == State::DOWNLOADING || current.state == State::INSTALLING) {
			return;
		}
		current.state = State::DOWNLOADING;
		current.error.clear();
		progress = std::make_shared<Network::TransferProgress>();
	}
	Utils::requestRedraw();
	const std::string path = updatePath();
	Utils::File::ensureDirectory(Utils::File::parentDirectory(path));
	std::string url = "https://raw.githubusercontent.com/" + repository() + "/" + BRANCH + "/TriCord.cia";
	Network::downloadFile(url, path, progress, [](bool success, long statusCode, const std::string &error) {
		if (!success) {
			Logger::log("[Updater] Download failed: %ld %s", statusCode, error.c_str());
			setState(State::FAILED, "download");
			return;
		}
		setState(State::INSTALLING);
		if (installer.joinable()) {
			installer.join();
		}
		installer.start(installWorker, 1, 32 * 1024);
	});
}

} // namespace Updater
} // namespace Core
