#ifndef TRPSHARE_UPLOAD_MANAGER_HPP
#define TRPSHARE_UPLOAD_MANAGER_HPP

#include "trpshare/Http.hpp"
#include <filesystem>
#include <map>

namespace trpshare {

// Sessions live until the server exits. Each acknowledged chunk advances offset.
class UploadManager {
public:
    ~UploadManager();
    void begin(int fd, const HttpRequest &request,
               const std::filesystem::path &destination, const std::string &relative);
    void handle(int fd, const HttpRequest &request);
private:
    struct Session {
        std::filesystem::path temporary, destination;
        std::string relative;
        std::uint64_t size = 0, offset = 0;
        bool completed = false;
    };
    std::map<std::string, Session> sessions_;
    void status(int fd, const std::string &id, const Session &session) const;
};
}
#endif
