#ifndef TRPSHARE_HTTP_HPP
#define TRPSHARE_HTTP_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace trpshare {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    std::uint64_t contentLength = 0;
};

bool readRequest(int fd, HttpRequest &request, std::string &error);
bool sendAll(int fd, const char *data, std::size_t size);
bool sendAll(int fd, const std::string &data);
void sendResponse(int fd, int code, const std::string &reason,
                  const std::string &contentType, const std::string &body,
                  const std::string &extraHeaders = "");
std::string urlDecode(const std::string &value);
std::string queryValue(const std::string &target, const std::string &key);
std::string jsonEscape(const std::string &value);

} // namespace trpshare

#endif
