#ifndef TRPSHARE_FILE_SERVICE_HPP
#define TRPSHARE_FILE_SERVICE_HPP

#include "trpshare/Http.hpp"
#include <filesystem>
#include <string>

namespace trpshare {

class FileService {
public:
    explicit FileService(std::filesystem::path shareRoot);
    void handle(int clientFd, const HttpRequest &request,
                const std::string &webPage) const;
    std::string describeRequest(const HttpRequest &request) const;

private:
    std::filesystem::path root_;
    std::string listFiles() const;
    void download(int clientFd, const std::string &relativePath) const;
    void upload(int clientFd, const HttpRequest &request) const;
};

} // namespace trpshare

#endif
