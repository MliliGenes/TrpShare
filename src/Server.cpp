#include "trpshare/Server.hpp"

#include "trpshare/FileService.hpp"
#include "trpshare/Http.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <utility>
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <csignal>
#include <unistd.h>
#include <ifaddrs.h>
#include <vector>

namespace trpshare {
namespace {
volatile std::sig_atomic_t keepRunning = 1;
void requestStop(int) { keepRunning = 0; }
}

Server::Server(unsigned short port, std::filesystem::path shareRoot,
               std::string webPage, Tui &tui)
    : port_(port), shareRoot_(std::move(shareRoot)),
      webPage_(std::move(webPage)), tui_(tui) {}

int Server::openListener() const {
    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) { perror("socket"); return -1; }

    int reuse = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if (bind(serverFd, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0) {
        perror("bind");
        close(serverFd);
        return -1;
    }
    if (listen(serverFd, 16) < 0) {
        perror("listen");
        close(serverFd);
        return -1;
    }
    return serverFd;
}

std::vector<std::string> Server::localAddresses() const {
    std::vector<std::string> result;
    result.push_back("http://127.0.0.1:" + std::to_string(port_));
    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return result;

    for (auto *entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
            !(entry->ifa_flags & IFF_UP) || (entry->ifa_flags & IFF_LOOPBACK))
            continue;
        char ip[INET_ADDRSTRLEN];
        const auto *address = reinterpret_cast<sockaddr_in *>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof ip))
            result.push_back("http://" + std::string(ip) + ":" + std::to_string(port_));
    }
    freeifaddrs(interfaces);
    return result;
}

int Server::run() {
    const int serverFd = openListener();
    if (serverFd < 0) return 1;
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    const auto addresses = localAddresses();
    if (!tui_.active()) {
        std::cout << "TrpShare is running\nShare: " << shareRoot_ << '\n';
        for (const auto &address : addresses) std::cout << "Open: " << address << '\n';
        std::cout << "Press Ctrl+C to stop.\n" << std::flush;
    }

    FileService files(shareRoot_);
    std::uint64_t requestCount = 0;
    std::string lastRequest = "Waiting for requests";
    std::string lastClient = "—";
    tui_.draw(shareRoot_.string(), addresses, requestCount, lastRequest, lastClient);
    keepRunning = 1;

    while (keepRunning) {
        if (tui_.shouldQuit()) break;
        pollfd descriptor{serverFd, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (ready == 0 || !(descriptor.revents & POLLIN)) continue;

        sockaddr_in peer{};
        socklen_t peerLength = sizeof peer;
        const int clientFd = accept(serverFd, reinterpret_cast<sockaddr *>(&peer), &peerLength);
        if (clientFd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        timeval timeout{30, 0};
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

        HttpRequest request;
        std::string error;
        if (readRequest(clientFd, request, error)) {
            ++requestCount;
            lastRequest = files.describeRequest(request);
            files.handle(clientFd, request, webPage_);
        } else {
            sendResponse(clientFd, 400, "Bad Request", "application/json",
                         "{\"error\":\"" + jsonEscape(error) + "\"}");
            lastRequest = "Bad request: " + error;
        }

        char ip[INET_ADDRSTRLEN] = "unknown";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        lastClient = ip;
        close(clientFd);
        tui_.draw(shareRoot_.string(), addresses, requestCount, lastRequest, lastClient);
    }

    close(serverFd);
    return 0;
}

} // namespace trpshare
