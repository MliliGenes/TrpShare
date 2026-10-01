#include "trpshare/FileService.hpp"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <vector>

namespace fs = std::filesystem;

namespace trpshare {
namespace {
bool safeName(const std::string &name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 240)
        return false;
    for (unsigned char c : name)
        if (c < ' ' || c == '/' || c == '\\' || c == ':' || c == 0)
            return false;
    return true;
}

bool safeRelativePath(const std::string &path) {
    if (path.empty() || path.size() > 2048 || path.front() == '/' || path.back() == '/')
        return false;
    std::stringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/'))
        if (!safeName(part)) return false;
    return true;
}

bool noSymlinkPath(const fs::path &root, const std::string &relative,
                   bool expectFile) {
    fs::path current = root;
    std::stringstream parts(relative);
    std::string part;
    std::vector<std::string> components;
    while (std::getline(parts, part, '/')) components.push_back(part);

    for (std::size_t i = 0; i < components.size(); ++i) {
        current /= components[i];
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error || status.type() == fs::file_type::symlink) return false;
        const bool last = i + 1 == components.size();
        if (!last && status.type() != fs::file_type::directory) return false;
        if (last && expectFile && status.type() != fs::file_type::regular) return false;
        if (last && !expectFile && status.type() != fs::file_type::directory) return false;
    }
    return true;
}

void sendError(int fd, int code, const std::string &reason,
               const std::string &message) {
    sendResponse(fd, code, reason, "application/json; charset=utf-8",
                 "{\"error\":\"" + jsonEscape(message) + "\"}");
}
} // namespace

FileService::FileService(fs::path shareRoot) : root_(std::move(shareRoot)) {}

std::string FileService::describeRequest(const HttpRequest &request) const {
    return request.method + " " + request.path;
}

std::string FileService::listFiles() const {
    std::ostringstream json;
    json << '[';
    bool first = true;
    std::error_code error;
    fs::recursive_directory_iterator it(
        root_, fs::directory_options::skip_permission_denied, error), end;

    for (; !error && it != end; it.increment(error)) {
        std::error_code itemError;
        const auto status = it->symlink_status(itemError);
        if (itemError) continue;
        if (status.type() == fs::file_type::symlink) {
            if (it->is_directory(itemError)) it.disable_recursion_pending();
            continue;
        }

        const bool isDirectory = status.type() == fs::file_type::directory;
        const bool isFile = status.type() == fs::file_type::regular;
        if (!isDirectory && !isFile) continue;
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.front() == '.') {
            if (isDirectory) it.disable_recursion_pending();
            continue;
        }

        const std::string path = fs::relative(it->path(), root_, itemError).generic_string();
        if (itemError) continue;
        std::uintmax_t size = 0;
        if (isFile) {
            size = it->file_size(itemError);
            if (itemError) continue;
        }

        if (!first) json << ',';
        first = false;
        json << "{\"name\":\"" << jsonEscape(name)
             << "\",\"path\":\"" << jsonEscape(path)
             << "\",\"type\":\"" << (isDirectory ? "directory" : "file")
             << "\",\"size\":" << size << '}';
    }
    json << ']';
    return json.str();
}

void FileService::download(int clientFd, const std::string &relativePath) const {
    if (!safeRelativePath(relativePath) || !noSymlinkPath(root_, relativePath, true)) {
        sendError(clientFd, 404, "Not Found", "file not found");
        return;
    }

    const fs::path path = root_ / fs::path(relativePath);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        sendError(clientFd, 404, "Not Found", "file unavailable");
        return;
    }
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) {
        sendError(clientFd, 500, "Internal Server Error", "could not read file size");
        return;
    }

    std::string downloadName = path.filename().string();
    for (char &c : downloadName)
        if (c == '"' || c == '\\') c = '_';

    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
           << "Content-Length: " << size << "\r\nContent-Disposition: attachment; filename=\""
           << downloadName << "\"\r\nConnection: close\r\n"
           << "X-Content-Type-Options: nosniff\r\n\r\n";
    if (!sendAll(clientFd, header.str())) return;

    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof buffer);
        const auto bytes = file.gcount();
        if (bytes > 0 && !sendAll(clientFd, buffer, static_cast<std::size_t>(bytes)))
            return;
    }
}

void FileService::beginUpload(int clientFd, const HttpRequest &request) {
    const std::string name = queryValue(request.target, "name");
    const std::string folder = queryValue(request.target, "path");
    if (!safeName(name) || (!folder.empty() &&
        (!safeRelativePath(folder) || !noSymlinkPath(root_, folder, false)))) {
        sendError(clientFd, 400, "Bad Request", "invalid upload path or filename");
        return;
    }
    const fs::path directory = folder.empty() ? root_ : root_ / fs::path(folder);
    const std::string relative = folder.empty() ? name : folder + "/" + name;
    uploads_.begin(clientFd, request, directory / name, relative);
}

void FileService::handle(int clientFd, const HttpRequest &request,
                         const std::string &webPage) {
    if (request.method == "GET" && request.path == "/") {
        sendResponse(clientFd, 200, "OK", "text/html; charset=utf-8", webPage);
    } else if (request.method == "GET" && request.path == "/upload.js") {
        std::ifstream script("web/upload.js", std::ios::binary);
        if (!script) { sendError(clientFd, 404, "Not Found", "upload script unavailable"); return; }
        const std::string body((std::istreambuf_iterator<char>(script)), {});
        sendResponse(clientFd, 200, "OK", "text/javascript; charset=utf-8", body);
    } else if (request.method == "GET" && request.path == "/api/files") {
        sendResponse(clientFd, 200, "OK", "application/json; charset=utf-8", listFiles());
    } else if (request.method == "GET" && request.path.rfind("/files/", 0) == 0) {
        download(clientFd, urlDecode(request.path.substr(7)));
    } else if (request.method == "POST" && request.path == "/api/uploads") {
        beginUpload(clientFd, request);
    } else if (request.path.rfind("/api/uploads/", 0) == 0) {
        uploads_.handle(clientFd, request);
    } else {
        sendError(clientFd, 404, "Not Found", "route not found");
    }
}

} // namespace trpshare
