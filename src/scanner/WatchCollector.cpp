#include "WatchCollector.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include "system/ProcessControl.h"

namespace fs = std::filesystem;

namespace {

// Returns the target of a symlink (for example "socket:[1234]"), or "" on failure.
std::string read_link(const fs::path& p) {
    char buf[PATH_MAX];
    ssize_t len = ::readlink(p.c_str(), buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    return std::string(buf, static_cast<size_t>(len));
}

// Converts "socket:[12345]" into 12345. Returns nothing for any other text.
std::optional<uint64_t> parse_socket_inode(std::string_view target) {
    constexpr std::string_view prefix = "socket:[";
    if (!target.starts_with(prefix) || target.back() != ']') return std::nullopt;

    auto digits = target.substr(prefix.size(), target.size() - prefix.size() - 1);
    uint64_t inode = 0;
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), inode);
    if (ec != std::errc{} || ptr != digits.data() + digits.size()) return std::nullopt;
    return inode;
}

// Reads /proc/net/tcp or /proc/net/tcp6 and maps socket inode -> listening port.
// Built once per refresh and shared by all processes.
std::unordered_map<uint64_t, int> build_inode_port_map(const std::string& net_path) {
    std::unordered_map<uint64_t, int> map;
    std::ifstream f(net_path);
    if (!f.is_open()) return map;

    std::string line;
    std::getline(f, line);                       // skip the header line

    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string tok[10];                     // token 1 = local address, 3 = state, 9 = inode
        bool ok = true;
        for (auto& t : tok)
            if (!(iss >> t)) { ok = false; break; }
        if (!ok || tok[3] != "0A") continue;     // "0A" means LISTEN

        const auto colon = tok[1].rfind(':');
        if (colon == std::string::npos) continue;

        int port = 0;
        const char* pf = tok[1].data() + colon + 1;
        const char* pl = tok[1].data() + tok[1].size();
        if (std::from_chars(pf, pl, port, 16).ec != std::errc{}) continue;   // port is hex

        uint64_t inode = 0;
        const std::string& s = tok[9];
        if (std::from_chars(s.data(), s.data() + s.size(), inode).ec != std::errc{}) continue;

        if (port <= 0 || port > 65535 || inode == 0) continue;
        map[inode] = port;
    }
    return map;
}

} // namespace

// For every process: walk its file descriptors ONCE, collecting inotify watches
// and socket inodes, then translate the sockets into listening ports.
std::vector<ProcessWatchInfo> WatchCollector::collect() {
    std::vector<ProcessWatchInfo> result;

    const auto tcp4 = build_inode_port_map("/proc/net/tcp");
    const auto tcp6 = build_inode_port_map("/proc/net/tcp6");

    for (const auto& proc : ProcScanner::scan()) {
        const std::string base = "/proc/" + std::to_string(proc.pid);

        // Fails quietly for other users' processes (no permission) or exited processes.
        std::error_code ec;
        fs::directory_iterator it(base + "/fd", ec);
        if (ec) continue;

        ProcessWatchInfo info;
        info.pid  = proc.pid;
        info.name = proc.name;
        std::set<uint64_t> socket_inodes;

        for (; !ec && it != fs::directory_iterator{}; it.increment(ec)) {
            const std::string fd_name = it->path().filename().string();
            const std::string target  = read_link(it->path());
            if (target.empty()) continue;

            if (target.starts_with("anon_inode:inotify")) {
                int fd = -1;
                std::from_chars(fd_name.data(), fd_name.data() + fd_name.size(), fd);

                auto watches = InotifyParser::parse_fdinfo(base + "/fdinfo/" + fd_name);
                for (auto& w : watches) {
                    w.fd = fd;
                    info.watches.push_back(w);
                }
            } else if (auto inode = parse_socket_inode(target)) {
                socket_inodes.insert(*inode);
            }
        }

        info.watch_count = info.watches.size();
        if (info.watch_count == 0) continue;     // not an inotify user, skip
        info.start_ticks = ProcessControl::start_ticks(proc.pid);

        // Match this process's sockets against the listening-port tables.
        std::set<int> ports;
        for (uint64_t inode : socket_inodes) {
            if (auto p4 = tcp4.find(inode); p4 != tcp4.end()) ports.insert(p4->second);
            if (auto p6 = tcp6.find(inode); p6 != tcp6.end()) ports.insert(p6->second);
        }
        for (int port : ports)
            info.listening_ports.push_back(std::to_string(port));

        result.push_back(std::move(info));
    }
    return result;
}