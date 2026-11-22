#include "network/dns.h"
#include "log.h"
#include "network/http_client.h"
#include <3ds.h>
#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <rapidjson/document.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace Network {
namespace Dns {

namespace {

constexpr u64 CACHE_MS = 10 * 60 * 1000;
constexpr int UDP_TIMEOUT_MS = 1500;
const char *const UDP_SERVERS[] = {"8.8.8.8", "8.8.4.4"};

std::atomic<bool> enabled{false};

struct Entry {
	std::string ip;
	u64 expires = 0;
};
std::mutex cacheMutex;
std::map<std::string, Entry> cache;

bool plausible(const std::string &ip) {
	in_addr addr{};
	if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
		return false;
	}
	const uint32_t a = ntohl(addr.s_addr);
	// Blocking resolvers often answer with these instead of saying no.
	return (a >> 24) != 0 && (a >> 24) != 127 && (a >> 24) != 10 && (a >> 16) != 0xC0A8 && (a >> 20) != 0xAC1;
}

// DNS-over-HTTPS: an ordinary HTTPS request to Google, which a network cannot quietly rewrite.
std::string queryHttps(const std::string &host) {
	CURL *easy = curl_easy_init();
	if (!easy) {
		return "";
	}
	HttpRequest request{"https://dns.google/resolve?type=A&name=" + host, "GET", "", {{"Accept", "application/json"}},
	                    6};
	request.resolveOverride = "dns.google:443:8.8.8.8";
	HttpResponse response;
	TransferResources resources = prepareTransfer(easy, request, response);
	const CURLcode result = curl_easy_perform(easy);
	resources.release();
	curl_easy_cleanup(easy);
	if (result != CURLE_OK) {
		Logger::log("[DNS] DoH lookup for %s failed: %s", host.c_str(), curl_easy_strerror(result));
		return "";
	}

	rapidjson::Document doc;
	doc.Parse(response.body.c_str());
	if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("Answer") || !doc["Answer"].IsArray()) {
		return "";
	}
	for (const auto &answer : doc["Answer"].GetArray()) {
		if (answer.IsObject() && answer.HasMember("type") && answer["type"].IsInt() && answer["type"].GetInt() == 1 &&
		    answer.HasMember("data") && answer["data"].IsString()) {
			return answer["data"].GetString();
		}
	}
	return "";
}

size_t skipName(const std::vector<uint8_t> &msg, size_t pos) {
	while (pos < msg.size()) {
		const uint8_t len = msg[pos];
		if (len == 0) {
			return pos + 1;
		}
		if ((len & 0xC0) == 0xC0) {
			return pos + 2;
		}
		pos += len + 1;
	}
	return msg.size();
}

// A plain A-record query over UDP port 53.
std::string queryUdp(const char *server, const std::string &host) {
	std::vector<uint8_t> query;
	const uint16_t id = (uint16_t)(osGetTime() ^ rand());
	const uint8_t header[12] = {(uint8_t)(id >> 8), (uint8_t)id, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
	query.insert(query.end(), header, header + 12);
	size_t start = 0;
	while (start <= host.size()) {
		size_t dot = host.find('.', start);
		if (dot == std::string::npos) {
			dot = host.size();
		}
		const size_t len = dot - start;
		if (len == 0 || len > 63) {
			return "";
		}
		query.push_back((uint8_t)len);
		query.insert(query.end(), host.begin() + start, host.begin() + dot);
		start = dot + 1;
		if (dot == host.size()) {
			break;
		}
	}
	const uint8_t tail[5] = {0, 0, 1, 0, 1};
	query.insert(query.end(), tail, tail + 5);

	const int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return "";
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(53);
	inet_pton(AF_INET, server, &addr.sin_addr);

	std::string result;
	if (sendto(fd, query.data(), query.size(), 0, (sockaddr *)&addr, sizeof(addr)) == (ssize_t)query.size()) {
		pollfd waiter{fd, POLLIN, 0};
		if (poll(&waiter, 1, UDP_TIMEOUT_MS) > 0) {
			std::vector<uint8_t> reply(512);
			const ssize_t got = recv(fd, reply.data(), reply.size(), 0);
			if (got >= 12) {
				reply.resize(got);
				const bool ours = reply[0] == (uint8_t)(id >> 8) && reply[1] == (uint8_t)id;
				const int rcode = reply[3] & 0x0F;
				const int questions = (reply[4] << 8) | reply[5];
				const int answers = (reply[6] << 8) | reply[7];
				size_t pos = 12;
				for (int i = 0; i < questions; i++) {
					pos = skipName(reply, pos) + 4;
				}
				for (int i = 0; ours && rcode == 0 && i < answers && pos + 10 <= reply.size(); i++) {
					pos = skipName(reply, pos);
					if (pos + 10 > reply.size()) {
						break;
					}
					const int type = (reply[pos] << 8) | reply[pos + 1];
					const int length = (reply[pos + 8] << 8) | reply[pos + 9];
					pos += 10;
					if (type == 1 && length == 4 && pos + 4 <= reply.size()) {
						char text[INET_ADDRSTRLEN];
						inet_ntop(AF_INET, &reply[pos], text, sizeof(text));
						result = text;
						break;
					}
					pos += length;
				}
			}
		}
	}
	close(fd);
	return result;
}

} // namespace

void setEnabled(bool on) {
	if (enabled.exchange(on) != on) {
		Logger::log("[DNS] Google DNS %s", on ? "on" : "off");
	}
}

bool isEnabled() { return enabled; }

std::string hostOf(const std::string &url) {
	size_t start = url.find("://");
	start = start == std::string::npos ? 0 : start + 3;
	size_t end = url.find_first_of(":/?#", start);
	return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string resolve(const std::string &host) {
	in_addr literal{};
	if (host.empty() || inet_pton(AF_INET, host.c_str(), &literal) == 1) {
		return host;
	}
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		auto it = cache.find(host);
		if (it != cache.end() && it->second.expires > osGetTime()) {
			return it->second.ip;
		}
	}

	std::string ip = queryHttps(host);
	if (!plausible(ip)) {
		ip.clear();
		for (const char *server : UDP_SERVERS) {
			ip = queryUdp(server, host);
			if (plausible(ip)) {
				break;
			}
			ip.clear();
		}
	}
	Logger::log("[DNS] %s -> %s", host.c_str(), ip.empty() ? "(failed)" : ip.c_str());
	if (!ip.empty()) {
		std::lock_guard<std::mutex> lock(cacheMutex);
		cache[host] = {ip, osGetTime() + CACHE_MS};
	}
	return ip;
}

} // namespace Dns
} // namespace Network
