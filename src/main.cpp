#include "trpshare/Server.hpp"
#include "trpshare/Tui.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char **argv) {
    unsigned short port = 8080;
    fs::path shareRoot = "shared";
    const fs::path pagePath = "web/index.html";

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) {
            const int value = std::atoi(argv[++i]);
            if (value < 1 || value > 65535) {
                std::cerr << "Invalid port\n";
                return 2;
            }
            port = static_cast<unsigned short>(value);
        } else if (argument == "--share" && i + 1 < argc) {
            shareRoot = argv[++i];
        } else if (argument == "--help") {
            std::cout << "Usage: trpshare [--port 8080] [--share ./shared]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }

    std::error_code error;
    fs::create_directories(shareRoot, error);
    if (error) {
        std::cerr << "Cannot create share directory: " << error.message() << '\n';
        return 1;
    }
    shareRoot = fs::weakly_canonical(shareRoot, error);
    if (error) {
        std::cerr << "Cannot resolve share directory\n";
        return 1;
    }

    std::ifstream pageFile(pagePath, std::ios::binary);
    if (!pageFile) {
        std::cerr << "Cannot open " << pagePath
                  << " (run from the project directory)\n";
        return 1;
    }
    const std::string webPage((std::istreambuf_iterator<char>(pageFile)), {});

    trpshare::Tui tui;
    tui.start();
    trpshare::Server server(port, shareRoot, webPage, tui);
    return server.run();
}
