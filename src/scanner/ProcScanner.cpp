#include "ProcScanner.h"

#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>

namespace {

// Replaces control characters with '?'. A process name is chosen by its owner,
// so it must never reach the terminal raw. Covers ESC, BEL, newline, DEL and the
// UTF-8 form of C1 controls (U+0080..U+009F = bytes 0xC2 0x80..0x9F).
std::string sanitize(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x20 || c == 0x7f) { out += '?'; continue; }
        if (c == 0xC2 && i + 1 < in.size() &&
            static_cast<unsigned char>(in[i + 1]) >= 0x80 &&
            static_cast<unsigned char>(in[i + 1]) <= 0x9F) {
            out += '?';
            ++i;
            continue;
        }
        out += static_cast<char>(c);
    }
    return out;
}

// "1234" -> 1234. Returns 0 for anything that is not a plain positive number.
int parse_pid(const char* s) {
    if (*s == '\0') return 0;
    long value = 0;
    for (; *s != '\0'; ++s) {
        if (*s < '0' || *s > '9') return 0;
        value = value * 10 + (*s - '0');
        if (value > 100000000) return 0;   // pid_max is far below this
    }
    return static_cast<int>(value);
}

} // namespace

// Lists /proc once with opendir/readdir (no std::filesystem objects, no extra syscalls).
std::vector<int> ProcScanner::list_pids() {
    std::vector<int> pids;
    DIR* dir = ::opendir("/proc");
    if (!dir) return pids;
    while (const dirent* e = ::readdir(dir)) {
        if (const int pid = parse_pid(e->d_name); pid > 0)
            pids.push_back(pid);
    }
    ::closedir(dir);
    return pids;
}

// One open/read/close of a tiny file. Never throws.
std::string ProcScanner::read_name(int pid) {
    char path[48];
    std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    char buf[64];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    if (n <= 0) return {};
    size_t len = static_cast<size_t>(n);
    if (buf[len - 1] == '\n') --len;
    return sanitize(std::string_view(buf, len));
}

std::vector<ProcessInfo> ProcScanner::scan() {
    std::vector<ProcessInfo> processes;
    for (const int pid : list_pids()) {
        std::string name = read_name(pid);
        if (!name.empty())
            processes.push_back({pid, std::move(name)});
    }
    return processes;
}
