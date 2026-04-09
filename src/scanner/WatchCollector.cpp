#include "WatchCollector.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <limits.h>
#include <iostream>   

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

std::vector<ProcessWatchInfo> WatchCollector::collect() {
    std::vector<ProcessWatchInfo> result;
    auto processes = ProcScanner::scan();

    std::cerr << "[DEBUG] Found " << processes.size() << " processes\n";

    for (const auto& proc : processes) {
        ProcessWatchInfo info;
        info.pid = proc.pid;
        info.name = proc.name;

        std::string fd_dir = "/proc/" + std::to_string(proc.pid) + "/fd";
        std::error_code ec;
        if (!fs::exists(fd_dir, ec) || !fs::is_directory(fd_dir, ec)) {
            continue; // Skip processes we can't access (permissions)
        }

        for (const auto& entry : fs::directory_iterator(fd_dir, ec)) {
            if (ec) break; // Stop on error
            if (!entry.is_symlink()) continue;

            std::string fd_name = entry.path().filename().string();
            std::string fdinfo_path = "/proc/" + std::to_string(proc.pid) + "/fdinfo/" + fd_name;

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
        if (info.watch_count > 0) {
            result.push_back(std::move(info));
        }
    }
    std::cerr << "[DEBUG] Collected " << result.size() << " processes with watches\n";
    return result;
}