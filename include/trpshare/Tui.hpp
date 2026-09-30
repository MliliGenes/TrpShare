#ifndef TRPSHARE_TUI_HPP
#define TRPSHARE_TUI_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace trpshare {

class Tui {
public:
    Tui();
    ~Tui();
    Tui(const Tui &) = delete;
    Tui &operator=(const Tui &) = delete;

    void start();
    bool active() const;
    void stop();
    bool shouldQuit();
    void draw(const std::string &sharePath,
              const std::vector<std::string> &addresses,
              std::uint64_t requestCount,
              const std::string &lastRequest,
              const std::string &lastClient);

private:
    bool started_;
};

} // namespace trpshare

#endif
