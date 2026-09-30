#ifndef TRPSHARE_SERVER_HPP
#define TRPSHARE_SERVER_HPP

#include "trpshare/Tui.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace trpshare {

class Server {
public:
    Server(unsigned short port, std::filesystem::path shareRoot,
           std::string webPage, Tui &tui);
    int run();

private:
    unsigned short port_;
    std::filesystem::path shareRoot_;
    std::string webPage_;
    Tui &tui_;

    int openListener() const;
    std::vector<std::string> localAddresses() const;
};

} // namespace trpshare

#endif
