#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <atomic>
#include <cstdint>
#include <curl/curl.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Network {

struct HttpResponse {
	long statusCode = 0;
	std::string body;
	std::string date;
	std::string retryAfter;
	bool success = false;
	std::string error;
};

// One field of a multipart/form-data body. A part with a filePath is streamed
// from the SD card rather than held in memory.
struct MultipartPart {
	std::string name;
	std::string data;
	std::string filePath;
	std::string filename;
	std::string contentType;
};

// Shared between the transfer and whoever shows it, so progress can be read
// from the UI thread and a transfer can be called off from there.
struct TransferProgress {
	std::atomic<int64_t> done{0};
	std::atomic<int64_t> total{0};
	std::atomic<bool> cancel{false};
};

struct HttpRequest {
	std::string url;
	std::string method;
	std::string body;
	std::map<std::string, std::string> headers;
	long timeoutSeconds;
	// Sent as multipart/form-data instead of body when not empty.
	std::vector<MultipartPart> multipart = {};
	// The response body goes to this file instead of HttpResponse::body.
	std::string downloadPath = {};
	std::shared_ptr<TransferProgress> progress = nullptr;
	// "host:port:address", which bypasses DNS for this request; used for the DNS-over-HTTPS lookups themselves.
	std::string resolveOverride = {};
};

// What a transfer holds on to until it finishes.
struct TransferResources {
	curl_slist *headers = nullptr;
	curl_mime *mime = nullptr;
	FILE *file = nullptr;
	curl_slist *resolve = nullptr;

	void release();
};

const std::string &clientProperties();
void initHttpDefaults();
TransferResources prepareTransfer(CURL *easy, const HttpRequest &request, HttpResponse &response);

class HttpClient {
  public:
	HttpResponse get(const std::string &url, const std::map<std::string, std::string> &extraHeaders = {});
	HttpResponse post(const std::string &url, const std::string &body,
	                  const std::map<std::string, std::string> &extraHeaders = {});
	HttpResponse patch(const std::string &url, const std::string &body,
	                   const std::map<std::string, std::string> &extraHeaders = {});
	HttpResponse put(const std::string &url, const std::string &body = "",
	                 const std::map<std::string, std::string> &extraHeaders = {});
	HttpResponse del(const std::string &url, const std::map<std::string, std::string> &extraHeaders = {});

	void setAuthToken(const std::string &token) { authToken = token; }
	void setTimeout(long seconds) { timeout = seconds; }

  private:
	HttpResponse perform(const std::string &url, const std::string &method, const std::string &body,
	                     const std::map<std::string, std::string> &extraHeaders);

	std::string authToken;
	long timeout = 0;
};

} // namespace Network

#endif // HTTP_CLIENT_H
