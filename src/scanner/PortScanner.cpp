#include "PortScanner.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_set>
#include <cstdint>
#include <regex>
#include <unistd.h>

namespace fs = std::filesystem;

static std::unordered_set<int> getSocketInodesForPid(int pid) {
    std::unordered_set<int> inodes;
    std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fdDir, ec)) {
        if (ec) break;
        std::string link = entry.path().string();
        char target[256];
        ssize_t len = readlink(link.c_str(), target, sizeof(target)-1);
        if (len <= 0) continue;
        target[len] = '\0';
        std::string targetStr(target);
        // Look for "socket:[inode]"
        if (targetStr.find("socket:[") == 0) {
            int inode = std::stoi(targetStr.substr(8, targetStr.size()-9));
            inodes.insert(inode);
        }
    }
    return inodes;
}

std::unordered_map<int, std::vector<std::string>> PortScanner::scanListeningPorts() {
    std::unordered_map<int, std::vector<std::string>> result;

    // Collect all socket inodes per PID
    std::unordered_map<int, std::unordered_set<int>> pidToInodes;
    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), ::isdigit)) continue;
        int pid = std::stoi(name);
        auto inodes = getSocketInodesForPid(pid);
        if (!inodes.empty())
            pidToInodes[pid] = std::move(inodes);
    }

    // Parse /proc/net/tcp for listening sockets
    auto parseNetFile = [&](const std::string& path, const std::string& proto) {
        std::ifstream file(path);
        if (!file.is_open()) return;
        std::string line;
        std::getline(file, line); // skip header
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string token;
            std::vector<std::string> tokens;
            while (iss >> token) tokens.push_back(token);
            if (tokens.size() < 10) continue;
            // State is tokens[3] (e.g., "0A" for LISTEN)
            if (tokens[3] != "0A") continue; // 0A = TCP_LISTEN
            // Local address: tokens[1] (hex IP:port)
            size_t colon = tokens[1].rfind(':');
            if (colon == std::string::npos) continue;
            uint16_t port = std::stoi(tokens[1].substr(colon+1), nullptr, 16);
            // inode is tokens[9]
            int inode = std::stoi(tokens[9]);
            // Find which PID owns this socket
            for (auto& [pid, inodes] : pidToInodes) {
                if (inodes.count(inode)) {
                    result[pid].push_back(std::to_string(port));
                    break;
                }
            }
        }
    };

    parseNetFile("/proc/net/tcp", "tcp");
    parseNetFile("/proc/net/tcp6", "tcp6");
    // Optionally parse UDP files ("/proc/net/udp", "/proc/net/udp6") if needed.

    return result;
}