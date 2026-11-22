#include "WatchCollector.h"
#include "system/ProcessControl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

using PortMap = std::unordered_map<uint64_t, int>;   // socket inode -> listening port

// "socket:[12345]" -> 12345. Returns 0 for any other text.
uint64_t socket_inode(std::string_view target) {
    constexpr std::string_view prefix = "socket:[";
    if (target.size() <= prefix.size() + 1 || !target.starts_with(prefix) || target.back() != ']')
        return 0;
    const auto digits = target.substr(prefix.size(), target.size() - prefix.size() - 1);
    if (digits.size() > 19) return 0;
    uint64_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return 0;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    return value;
}

// Reads /proc/net/tcp or tcp6 and adds "socket inode -> port" for LISTEN sockets only.
// A line looks like:  "0: 0100007F:0BB8 00000000:0000 0A 00000000:00000000 ... 1000 0 12345 ..."
// token 1 = local address:port (hex), token 3 = state (0A = LISTEN), token 9 = inode.
// Most lines are not LISTEN, so a line is dropped as soon as its state is known.
void read_listening(const char* path, PortMap& out) {
    FILE* f = std::fopen(path, "re");
    if (!f) return;

    char line[1024];
    bool header = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (header) { header = false; continue; }

        const char* tok[10];
        size_t len[10];
        int n = 0;
        const char* p = line;
        while (n < 10) {
            while (*p == ' ') ++p;
            if (*p == '\0' || *p == '\n') break;
            tok[n] = p;
            while (*p != ' ' && *p != '\0' && *p != '\n') ++p;
            len[n] = static_cast<size_t>(p - tok[n]);
            ++n;
            if (n == 4 && !(len[3] == 2 && tok[3][0] == '0' && tok[3][1] == 'A'))
                break;                                   // not listening: skip the rest
        }
        if (n < 10) continue;

        const char* colon = nullptr;
        for (const char* q = tok[1]; q < tok[1] + len[1]; ++q)
            if (*q == ':') colon = q;
        if (!colon) continue;

        const unsigned long port = std::strtoul(colon + 1, nullptr, 16);
        const unsigned long long inode = std::strtoull(tok[9], nullptr, 10);
        if (port == 0 || port > 65535 || inode == 0) continue;
        out[inode] = static_cast<int>(port);
    }
    std::fclose(f);
}

} // namespace

// For every process: walk its fd folder ONCE, collecting inotify watches and socket inodes.
// Only processes that really own watches cost more (name, start time, ports).
// The listening-port tables are read lazily, and only if some process needs them.
std::vector<ProcessWatchInfo> WatchCollector::collect() {
    std::vector<ProcessWatchInfo> result;

    PortMap tcp4, tcp6;
    bool ports_loaded = false;
    std::vector<uint64_t> sockets;

    for (const int pid : ProcScanner::list_pids()) {
        char fd_dir_path[48];
        std::snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);

        // Fails quietly for other users' processes (no permission) and for exited ones.
        DIR* dir = ::opendir(fd_dir_path);
        if (!dir) continue;
        const int dir_fd = ::dirfd(dir);

        std::vector<WatchDescriptor> watches;
        sockets.clear();

        while (const dirent* e = ::readdir(dir)) {
            if (e->d_name[0] == '.') continue;

            // Only the start of the link target is needed, so a small buffer is enough.
            char target[256];
            const ssize_t len = ::readlinkat(dir_fd, e->d_name, target, sizeof(target) - 1);
            if (len <= 0) continue;
            const std::string_view link(target, static_cast<size_t>(len));

            if (link.starts_with("anon_inode:inotify")) {
                const int fd = static_cast<int>(std::strtol(e->d_name, nullptr, 10));
                char info_path[64];
                std::snprintf(info_path, sizeof(info_path), "/proc/%d/fdinfo/%d", pid, fd);
                for (auto& w : InotifyParser::parse_fdinfo(info_path)) {
                    w.fd = fd;
                    watches.push_back(w);
                }
            } else if (const uint64_t inode = socket_inode(link)) {
                sockets.push_back(inode);
            }
        }
        ::closedir(dir);

        if (watches.empty()) continue;               // not an inotify user

        std::string name = ProcScanner::read_name(pid);
        if (name.empty()) continue;                  // process exited meanwhile

        ProcessWatchInfo info;
        info.pid         = pid;
        info.name        = std::move(name);
        info.watch_count = watches.size();
        info.watches     = std::move(watches);
        info.start_ticks = ProcessControl::start_ticks(pid);

        if (!sockets.empty()) {
            if (!ports_loaded) {
                read_listening("/proc/net/tcp", tcp4);
                read_listening("/proc/net/tcp6", tcp6);
                ports_loaded = true;
            }
            std::vector<int> ports;
            for (const uint64_t inode : sockets) {
                if (const auto p4 = tcp4.find(inode); p4 != tcp4.end()) ports.push_back(p4->second);
                if (const auto p6 = tcp6.find(inode); p6 != tcp6.end()) ports.push_back(p6->second);
            }
            std::sort(ports.begin(), ports.end());
            ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
            for (const int port : ports)
                info.listening_ports.push_back(std::to_string(port));
        }

        result.push_back(std::move(info));
    }
    return result;
}
