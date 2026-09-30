#include "trpshare/Tui.hpp"

#include <algorithm>
#include <curses.h>
#include <unistd.h>

namespace trpshare {
namespace {
void putLine(int row, int column, const std::string &text, int maxWidth) {
    if (maxWidth <= 0) return;
    mvaddnstr(row, column, text.c_str(), maxWidth);
}
} // namespace

Tui::Tui() : started_(false) {}
Tui::~Tui() { stop(); }

void Tui::start() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;
    if (initscr() == nullptr) return;
    started_ = true;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
}

bool Tui::active() const { return started_; }

void Tui::stop() {
    if (!started_) return;
    nodelay(stdscr, FALSE);
    echo();
    nocbreak();
    endwin();
    started_ = false;
}

bool Tui::shouldQuit() {
    if (!started_) return false;
    const int key = getch();
    return key == 'q' || key == 'Q';
}

void Tui::draw(const std::string &sharePath,
               const std::vector<std::string> &addresses,
               std::uint64_t requestCount,
               const std::string &lastRequest,
               const std::string &lastClient) {
    if (!started_) return;
    erase();
    int rows, columns;
    getmaxyx(stdscr, rows, columns);
    if (rows < 9 || columns < 36) {
        putLine(0, 0, "Resize terminal for TrpShare · press q to quit", columns);
        refresh();
        return;
    }

    box(stdscr, 0, 0);
    attron(A_BOLD);
    putLine(1, 3, "TRPSHARE  /  LOCAL FILE TRANSFER", columns - 6);
    attroff(A_BOLD);
    putLine(3, 3, "STATUS", columns - 6);
    putLine(4, 3, "Online · serving the shared folder", columns - 6);
    putLine(6, 3, "SHARED DIRECTORY", columns - 6);
    putLine(7, 3, sharePath, columns - 6);

    int row = 9;
    putLine(row++, 3, "OPEN FROM A PHONE", columns - 6);
    for (const auto &address : addresses) {
        if (row >= rows - 5) break;
        putLine(row++, 3, address, columns - 6);
    }
    if (row < rows - 4) ++row;
    putLine(row++, 3, "REQUESTS  " + std::to_string(requestCount), columns - 6);
    putLine(row++, 3, "LAST      " + lastRequest, columns - 6);
    putLine(row++, 3, "CLIENT    " + lastClient, columns - 6);
    putLine(rows - 2, 3, "q  Quit", columns - 6);
    refresh();
}

} // namespace trpshare
