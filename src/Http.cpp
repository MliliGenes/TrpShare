#include "trpshare/Http.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <sstream>
#include <sys/socket.h>

namespace trpshare {
namespace {
constexpr std::size_t MAX_HEADER_SIZE = 32 * 1024;
constexpr std::uint64_t MAX_REQUEST_BODY = 2ULL * 1024 * 1024 * 1024;

std::string trim(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::string jsonEscape(const std::string &value) {
    std::string escaped;
    for (unsigned char c : value) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c < 32) {
                char buffer[7];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                escaped += buffer;
            } else {
                escaped += static_cast<char>(c);
            }
        }
    }
    return escaped;
}

std::string urlDecode(const std::string &value) {
    std::string decoded;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hexValue(value[i + 1]);
            const int low = hexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded += static_cast<char>((high << 4) | low);
                i += 2;
            } else {
                decoded += value[i];
            }
        } else {
            decoded += value[i];
        }
    }
    return decoded;
}

std::string queryValue(const std::string &target, const std::string &key) {
    const auto question = target.find('?');
    if (question == std::string::npos) return "";
    std::stringstream query(target.substr(question + 1));
    std::string item;
    while (std::getline(query, item, '&')) {
        const auto equal = item.find('=');
        if (equal != std::string::npos && item.substr(0, equal) == key)
            return urlDecode(item.substr(equal + 1));
    }
    return "";
}

bool readRequest(int fd, HttpRequest &request, std::string &error) {
    std::string data;
    char buffer[16384];
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        const ssize_t bytes = recv(fd, buffer, sizeof buffer, 0);
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes <= 0) { error = "incomplete request"; return false; }
        data.append(buffer, static_cast<std::size_t>(bytes));
        if (data.size() > MAX_HEADER_SIZE) {
            error = "request headers too large";
            return false;
        }
    }

    std::istringstream headers(data.substr(0, headerEnd));
    std::string line;
    if (!std::getline(headers, line)) { error = "bad request"; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream firstLine(line);
    std::string version;
    if (!(firstLine >> request.method >> request.target >> version) ||
        version.rfind("HTTP/1.", 0) != 0) {
        error = "bad request line";
        return false;
    }
    const auto question = request.target.find('?');
    request.path = request.target.substr(0, question);

    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) { error = "bad header"; return false; }
        request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    if (request.headers.count("transfer-encoding")) {
        error = "transfer encoding is not supported";
        return false;
    }
    const auto length = request.headers.find("content-length");
    if (length != request.headers.end()) {
        try {
            std::size_t used = 0;
            request.contentLength = std::stoull(length->second, &used);
            if (used != length->second.size()) throw std::runtime_error("invalid");
        } catch (...) {
            error = "invalid content length";
            return false;
        }
    }
    if (request.contentLength > MAX_REQUEST_BODY) {
        error = "upload exceeds 2 GiB limit";
        return false;
    }

    const std::size_t bodyStart = headerEnd + 4;
    if (data.size() > bodyStart) {
        const std::size_t available = data.size() - bodyStart;
        const std::size_t bodyBytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(available, request.contentLength));
        request.body.assign(data.data() + bodyStart, bodyBytes);
    }
    return true;
}

bool sendAll(int fd, const char *data, std::size_t size) {
    while (size > 0) {
        const ssize_t sent = send(fd, data, size, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool sendAll(int fd, const std::string &data) {
    return sendAll(fd, data.data(), data.size());
}

void sendResponse(int fd, int code, const std::string &reason,
                  const std::string &contentType, const std::string &body,
                  const std::string &extraHeaders) {
    std::ostringstream header;
    header << "HTTP/1.1 " << code << ' ' << reason
           << "\r\nContent-Type: " << contentType
           << "\r\nContent-Length: " << body.size()
           << "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n"
           << extraHeaders << "\r\n";
    sendAll(fd, header.str());
    sendAll(fd, body);
}

} // namespace trpshare
