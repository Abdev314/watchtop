#include "WatchDetails.h"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <charconv>

using namespace ftxui;

namespace {

constexpr size_t kMaxRows = 500;   // a process can own 100k+ watches; never draw them all

// 255 -> "0xff"
std::string to_hex(uint64_t v) {
    char buf[20];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, 16);
    return "0x" + std::string(buf, res.ptr);
}

} // namespace

WatchDetails::WatchDetails() {
    component_ = Renderer([this] {
        if (!current_)
            return text("No process selected") | center | size(HEIGHT, EQUAL, 20) | border;

        const ProcessWatchInfo& proc = *current_;
        Elements rows;

        // Fixed header.
        rows.push_back(text("Process: " + proc.name + " (PID: " + std::to_string(proc.pid) + ")") | bold);
        rows.push_back(separator());
        rows.push_back(hbox({
            text("FD")    | size(WIDTH, EQUAL, 6)  | bold,
            text("WD")    | size(WIDTH, EQUAL, 6)  | bold,
            text("Inode") | size(WIDTH, EQUAL, 14) | bold,
            text("Mask")  | bold,
        }));
        rows.push_back(separator());

        // Watch list, limited to kMaxRows entries.
        Elements list;
        const size_t shown = std::min(proc.watches.size(), kMaxRows);
        for (size_t i = 0; i < shown; ++i) {
            const auto& w = proc.watches[i];
            list.push_back(hbox({
                text(std::to_string(w.fd))    | size(WIDTH, EQUAL, 6),
                text(std::to_string(w.wd))    | size(WIDTH, EQUAL, 6),
                text(std::to_string(w.inode)) | size(WIDTH, EQUAL, 14),
                text(to_hex(w.mask)),
            }));
        }
        if (proc.watches.size() > shown)
            list.push_back(text("... " + std::to_string(proc.watches.size() - shown) + " more not shown") | dim);

        rows.push_back(vbox(std::move(list)) | yframe | vscroll_indicator | flex);
        rows.push_back(separator());
        rows.push_back(text("Find a path: find / -xdev -inum <inode>") | dim);

        return vbox(std::move(rows)) | border | size(HEIGHT, EQUAL, 20) | size(WIDTH, EQUAL, 60);
    });
}

ftxui::Component WatchDetails::GetComponent() { return component_; }
void WatchDetails::SetProcess(const ProcessWatchInfo* info) { current_ = info; }
void WatchDetails::Clear() { current_ = nullptr; }