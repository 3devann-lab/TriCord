#pragma once

#include "network/network_manager.h"
#include <cstdint>
#include <functional>
#include <string>

namespace Network {
namespace MediaCache {

void init();
void shutdown();

// GET url, answered from the SD card copy when there is one. Successful answers are
// stored for next time. Hits run the callback on the cache's own thread.
void fetch(const std::string &url, RequestPriority priority, ResponseCallback callback,
           std::map<std::string, std::string> headers = {});

// Runs job on the cache's thread, for other SD card work that should not hold up the UI.
void runJob(std::function<void()> job);

// Deletes every cached image and message list.
void clearAll();
// Bytes on the SD card, as of the last scan or clear (approximate in between).
uint64_t sizeBytes();

// Recent messages, kept per account and channel as the raw API answer.
std::string messagesPath(const std::string &userId, const std::string &channelId);
void storeMessages(const std::string &userId, const std::string &channelId, const std::string &json);

} // namespace MediaCache
} // namespace Network
