#include "WatchCollector.h"
#include "ProcScanner.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <limits.h>
#include <set> 

namespace fs = std::filesystem;

// ── Resolve an fd symlink to its target path ─────────────────────────────────
static std::string resolve_fd_path(int pid, const std::string& fd) {
    std::string link_path = "/proc/" + std::to_string(pid) + "/fd/" + fd;
    char buf[PATH_MAX];
    ssize_t len = readlink(link_path.c_str(), buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return {};
}

// ── Collect all inodes that this process has open sockets for ────────────────
// by reading /proc/<pid>/fd/ and collecting entries like "socket:[inode]"
static std::set<uint64_t> collect_socket_inodes(int pid) {
    std::set<uint64_t> inodes;
    std::string fd_dir = "/proc/" + std::to_string(pid) + "/fd";
    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(fd_dir, ec)) {
        if (ec) break;
        char buf[PATH_MAX];
        std::string path = entry.path().string();
        ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';
        std::string target(buf);
        // Socket symlinks look like "socket:[1234567]"
        if (target.rfind("socket:[", 0) == 0 && target.back() == ']') {
            try {
                uint64_t inode = std::stoull(target.substr(8, target.size() - 9));
                inodes.insert(inode);
            } catch (...) {}
        }
    }
    return inodes;
}

// ── Parse /proc/<pid>/net/tcp[6] for LISTEN rows ─────────────────────────────
// Returns ports (as strings) whose inodes are in the provided set.
// tcp format columns: sl local_addr rem_addr state tx:rx uid timeout inode ...
static std::vector<std::string> parse_tcp_ports(int pid,
                                                 const std::string& net_file,
                                                 const std::set<uint64_t>& inodes) {
    std::vector<std::string> ports;
    std::ifstream f("/proc/" + std::to_string(pid) + "/" + net_file);
    if (!f.is_open()) return ports;

    std::string line;
    std::getline(f, line); // skip header

    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string sl, local_addr, rem_addr, state;
        std::string tx_rx, uid, timeout;
        uint64_t inode;

        iss >> sl >> local_addr >> rem_addr >> state >> tx_rx >> uid >> timeout >> inode;

        // state "0A" = TCP_LISTEN
        if (state != "0A") continue;
        if (inodes.find(inode) == inodes.end()) continue;

        // local_addr is "hex_ip:hex_port" — extract port from after the colon
        auto colon = local_addr.find(':');
        if (colon == std::string::npos) continue;

        try {
            int port = std::stoi(local_addr.substr(colon + 1), nullptr, 16);
            ports.push_back(std::to_string(port));
        } catch (...) {}
    }
    return ports;
}

// ── Main collect ──────────────────────────────────────────────────────────────
std::vector<ProcessWatchInfo> WatchCollector::collect() {
    std::vector<ProcessWatchInfo> result;
    auto processes = ProcScanner::scan();

    for (const auto& proc : processes) {
        ProcessWatchInfo info;
        info.pid  = proc.pid;
        info.name = proc.name;

        std::string fd_dir = "/proc/" + std::to_string(proc.pid) + "/fd";
        std::error_code ec;
        if (!fs::exists(fd_dir, ec) || !fs::is_directory(fd_dir, ec))
            continue;

        // ── inotify watches ───────────────────────────────────────────────
        for (const auto& entry : fs::directory_iterator(fd_dir, ec)) {
            if (ec) break;
            if (!entry.is_symlink()) continue;

            std::string fd_name     = entry.path().filename().string();
            std::string fdinfo_path = "/proc/" + std::to_string(proc.pid)
                                    + "/fdinfo/" + fd_name;

            auto watches = InotifyParser::parse_fdinfo(fdinfo_path);
            if (watches.empty()) continue;

            std::string resolved = resolve_fd_path(proc.pid, fd_name);
            for (const auto& wd : watches) {
                info.watches.push_back(wd);
                if (!resolved.empty())
                    info.wd_to_path[wd.wd] = resolved;
            }
        }

        info.watch_count = info.watches.size();
        if (info.watch_count == 0)
            continue;

        // ── listening ports ───────────────────────────────────────────────
        // Only do the port scan for processes that actually have watches,
        // since we already filtered above.
        auto socket_inodes = collect_socket_inodes(proc.pid);
        if (!socket_inodes.empty()) {
            auto tcp4 = parse_tcp_ports(proc.pid, "net/tcp",  socket_inodes);
            auto tcp6 = parse_tcp_ports(proc.pid, "net/tcp6", socket_inodes);

            // Merge, deduplicate
            std::set<std::string> seen;
            for (const auto& p : tcp4) if (seen.insert(p).second) info.listening_ports.push_back(p);
            for (const auto& p : tcp6) if (seen.insert(p).second) info.listening_ports.push_back(p);
        }

        result.push_back(std::move(info));
    }
    return result;
}