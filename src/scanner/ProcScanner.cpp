#include "ProcScanner.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Replaces control characters (ESC, newline, ...) with '?'.
// A process name is chosen by its owner, so it must never reach the terminal raw.
std::string sanitize(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
        out += (c < 0x20 || c == 0x7f) ? '?' : static_cast<char>(c);
    return out;
}

} 

// Lists every numeric directory in /proc together with its process name.
// Never throws: processes that vanish during the scan are skipped.
std::vector<ProcessInfo> ProcScanner::scan() {
    std::vector<ProcessInfo> processes;

    std::error_code ec;
    fs::directory_iterator it("/proc", ec);
    for (; !ec && it != fs::directory_iterator{}; it.increment(ec)) {
        const std::string dirname = it->path().filename().string();

        // Accept only names made entirely of digits (no exceptions, unlike std::stoi).
        int pid = 0;
        const char* first = dirname.data();
        const char* last  = first + dirname.size();
        auto [ptr, err] = std::from_chars(first, last, pid);
        if (err != std::errc{} || ptr != last || pid <= 0) continue;

        std::ifstream comm("/proc/" + dirname + "/comm");
        std::string name;
        if (std::getline(comm, name))
            processes.push_back({pid, sanitize(name)});
    }
    return processes;
}