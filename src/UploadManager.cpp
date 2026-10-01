#include "trpshare/UploadManager.hpp"
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace trpshare {
namespace {
constexpr std::uint64_t CHUNK_SIZE = 8ULL * 1024 * 1024;
bool number(const std::string &text, std::uint64_t &value) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) return false;
    try { std::size_t used; value = std::stoull(text, &used); return used == text.size(); }
    catch (...) { return false; }
}
void error(int fd, int code, const std::string &message) {
    sendResponse(fd, code, code == 404 ? "Not Found" : code == 409 ? "Conflict" :
        code == 500 ? "Internal Server Error" : code == 503 ? "Service Unavailable" : "Bad Request",
        "application/json", "{\"error\":\"" + jsonEscape(message) + "\"}");
}
}

UploadManager::~UploadManager() {
    for (const auto &entry : sessions_) {
        if (!entry.second.completed) {
            std::error_code ignored;
            fs::remove(entry.second.temporary, ignored);
        }
    }
}

void UploadManager::status(int fd, const std::string &id, const Session &session) const {
    sendResponse(fd, 200, "OK", "application/json",
        "{\"id\":\"" + id + "\",\"path\":\"" + jsonEscape(session.relative) +
        "\",\"size\":" + std::to_string(session.size) +
        ",\"offset\":" + std::to_string(session.offset) +
        ",\"chunkSize\":" + std::to_string(CHUNK_SIZE) +
        ",\"completed\":" + (session.completed ? "true" : "false") + "}");
}

void UploadManager::begin(int fd, const HttpRequest &request,
                          const fs::path &destination, const std::string &relative) {
    std::uint64_t size;
    if (!number(queryValue(request.target, "size"), size) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        size > 9007199254740991ULL) {
        error(fd, 400, "invalid file size"); return;
    }
    std::string id = queryValue(request.target, "id");
    if (!id.empty()) {
        if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos) {
            error(fd, 400, "invalid upload ID"); return;
        }
        auto existing = sessions_.find(id);
        if (existing != sessions_.end()) {
            if (existing->second.destination != destination || existing->second.size != size) {
                error(fd, 409, "upload ID belongs to another file"); return;
            }
            status(fd, id, existing->second); return;
        }
    }
    std::size_t active = 0;
    for (const auto &entry : sessions_) if (!entry.second.completed) ++active;
    if (active >= 32) { error(fd, 503, "too many unfinished uploads; cancel one first"); return; }
    // Prevent two sessions from publishing different files to the same name.
    for (const auto &entry : sessions_)
        if (!entry.second.completed && entry.second.destination == destination) {
            error(fd, 409, "an upload for this filename is already active"); return;
        }
    std::string pattern = (destination.parent_path() / ".trpshare-upload-XXXXXX").string();
    std::vector<char> filename(pattern.begin(), pattern.end()); filename.push_back('\0');
    int temporaryFd = mkstemp(filename.data());
    if (temporaryFd < 0) { error(fd, 500, "could not create upload file"); return; }
    close(temporaryFd);
    if (id.empty()) {
        do {
            std::random_device random;
            std::ostringstream token;
            token << std::hex;
            for (int i = 0; i < 4; ++i) { token.width(8); token.fill('0'); token << random(); }
            id = token.str();
        } while (sessions_.count(id));
    }
    Session session;
    session.temporary = filename.data(); session.destination = destination;
    session.relative = relative; session.size = size;
    sessions_.emplace(id, session);
    status(fd, id, session);
}

void UploadManager::handle(int fd, const HttpRequest &request) {
    // Drain rejected chunks so closing the socket does not reset the HTTP response.
    const auto fail = [&](int code, const std::string &message) {
        std::uint64_t received = request.body.size();
        char discard[65536];
        while (received < request.contentLength) {
            const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof discard, request.contentLength - received));
            const ssize_t bytes = recv(fd, discard, want, 0);
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes <= 0) break;
            received += static_cast<std::uint64_t>(bytes);
        }
        error(fd, code, message);
    };
    const std::string tail = request.path.substr(std::string("/api/uploads/").size());
    const auto slash = tail.find('/');
    const std::string id = tail.substr(0, slash);
    const std::string action = slash == std::string::npos ? "" : tail.substr(slash + 1);
    auto found = sessions_.find(id);
    if (found == sessions_.end()) { fail(404, "upload session not found; start again"); return; }
    Session &session = found->second;
    if (request.method == "GET" && action.empty()) { status(fd, id, session); return; }
    if (request.method == "DELETE" && action.empty()) {
        if (!session.completed) {
            std::error_code ignored; fs::remove(session.temporary, ignored);
        }
        sessions_.erase(found);
        sendResponse(fd, 200, "OK", "application/json", "{\"cancelled\":true}"); return;
    }
    if (request.method == "POST" && action == "complete") {
        if (session.completed) { status(fd, id, session); return; }
        if (session.offset != session.size) { fail(409, "file is incomplete"); return; }
        std::error_code ec;
        if (fs::file_size(session.temporary, ec) != session.size || ec) {
            fail(500, "temporary file size does not match"); return;
        }
        // POSIX rename publishes the file atomically; leave existing files intact on failure.
        fs::rename(session.temporary, session.destination, ec);
        if (ec) { fail(500, "could not finalize upload"); return; }
        session.completed = true;
        status(fd, id, session); return;
    }
    if (request.method != "PUT" || !action.empty()) { fail(404, "route not found"); return; }
    std::uint64_t offset;
    if (!number(queryValue(request.target, "offset"), offset)) {
        fail(400, "invalid chunk offset"); return;
    }
    if (session.completed || offset != session.offset) {
        fail(409, "offset mismatch; fetch upload status before retrying"); return;
    }
    const auto length = request.contentLength;
    if (!request.headers.count("content-length") || length == 0 || length > CHUNK_SIZE ||
        length > session.size - session.offset) {
        fail(400, "invalid chunk length"); return;
    }
    std::fstream output(session.temporary, std::ios::binary | std::ios::in | std::ios::out);
    output.seekp(static_cast<std::streamoff>(offset));
    if (!output) { fail(500, "could not open upload file"); return; }
    std::uint64_t received = request.body.size();
    output.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
    char buffer[65536];
    while (received < length && output) {
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof buffer, length - received));
        const ssize_t bytes = recv(fd, buffer, want, 0);
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes <= 0) break;
        output.write(buffer, bytes); received += static_cast<std::uint64_t>(bytes);
    }
    output.flush();
    if (received != length || !output) {
        // Offset stays unchanged. The next attempt overwrites this partial chunk.
        error(fd, 400, "incomplete or failed chunk; retry at the same offset"); return;
    }
    output.close();
    if (!output) { error(fd, 500, "could not finish writing chunk"); return; }
    session.offset += length;
    status(fd, id, session);
}
}
