#include "WatchCollector.h"
#include "ProcScanner.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <limits.h>
#include <set>
#include <map>

namespace fs = std::filesystem;

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

static std::set<uint64_t> collect_socket_inodes(int pid) {
    std::set<uint64_t> inodes;
    std::string fd_dir = "/proc/" + std::to_string(pid) + "/fd";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fd_dir, ec)) {
        if (ec) break;
        char buf[PATH_MAX];
        ssize_t len = readlink(entry.path().c_str(), buf, sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';
        std::string target(buf, len);

        if (target.substr(0, 8) != "socket:[") continue;
        if (target.back() != ']') continue;

        try {
            uint64_t inode = std::stoull(target.substr(8, target.size() - 9));
            inodes.insert(inode);
        } catch (...) {}
    }
    return inodes;
}

// ── Parse /proc/net/tcp[6] into an inode→port map ────────────────────────────
static std::map<uint64_t, std::string> build_inode_port_map(const std::string& net_path) {
    std::map<uint64_t, std::string> inode_to_port;
    std::ifstream f(net_path);
    if (!f.is_open()) return inode_to_port;

    std::string line;
    std::getline(f, line); // skip header

    while (std::getline(f, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string tokens[10];

        // Read exactly 10 tokens — inode is token index 9
        bool ok = true;
        for (int i = 0; i < 10; ++i) {
            if (!(iss >> tokens[i])) { ok = false; break; }
        }
        if (!ok) continue;

        if (tokens[3] != "0A") continue;

        const std::string& local = tokens[1];
        auto colon = local.rfind(':'); 
        if (colon == std::string::npos) continue;

        int port = 0;
        try {
            port = std::stoi(local.substr(colon + 1), nullptr, 16);
        } catch (...) { continue; }

        if (port <= 0 || port > 65535) continue;

        uint64_t inode = 0;
        try {
            inode = std::stoull(tokens[9]);
        } catch (...) { continue; }

        if (inode == 0) continue;

        inode_to_port[inode] = std::to_string(port);
    }
    return inode_to_port;
}

std::vector<ProcessWatchInfo> WatchCollector::collect() {
    std::vector<ProcessWatchInfo> result;
    auto processes = ProcScanner::scan();

    // Build once, reuse for all processes
    auto inode_map_tcp4 = build_inode_port_map("/proc/net/tcp");
    auto inode_map_tcp6 = build_inode_port_map("/proc/net/tcp6");

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
        auto socket_inodes = collect_socket_inodes(proc.pid);
        if (!socket_inodes.empty()) {
            std::set<std::string> seen;
            for (uint64_t inode : socket_inodes) {
                auto it4 = inode_map_tcp4.find(inode);
                if (it4 != inode_map_tcp4.end())
                    if (seen.insert(it4->second).second)
                        info.listening_ports.push_back(it4->second);

                auto it6 = inode_map_tcp6.find(inode);
                if (it6 != inode_map_tcp6.end())
                    if (seen.insert(it6->second).second)
                        info.listening_ports.push_back(it6->second);
            }
        }

        result.push_back(std::move(info));
    }
    return result;
}