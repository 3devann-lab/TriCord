#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace Core {
namespace Updater {

enum class State { IDLE, CHECKING, UP_TO_DATE, AVAILABLE, DOWNLOADING, INSTALLING, INSTALLED, FAILED };

struct Status {
	State state = State::IDLE;
	// The commit the newest TriCord.cia was built from, short form.
	std::string latestSha;
	time_t latestTime = 0;
	// Titles of what changed since this build, newest first.
	std::vector<std::string> changes;
	int64_t done = 0;
	int64_t total = 0;
	std::string error;
};

// What CI wrote into romfs:/build_info.json. Empty sha for local builds.
struct BuildInfo {
	std::string sha;
	time_t time = 0;
	std::string repo;
};

const BuildInfo &build();
// The repository updates come from: the build's own, else the default.
std::string repository();

// Asks GitHub for the newest TriCord.cia on main. Safe to call repeatedly.
void checkAsync();
// Downloads TriCord.cia to the SD card and installs it over this title.
void installAsync();
Status status();

} // namespace Updater
} // namespace Core
