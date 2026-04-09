#include "InotifyParser.h"
#include <fstream>
#include <regex>
#include <string>
#include <cctype>

// Safe conversion from string to uint64_t with fallback
static uint64_t safe_stoull(const std::string& s) {
    if (s.empty()) return 0;
    try {
        // Handle hex prefix (0x...)
        if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
            return std::stoull(s.substr(2), nullptr, 16);
        }
        // Try base 0 (auto-detect decimal/hex), but catch any exception
        return std::stoull(s, nullptr, 0);
    } catch (const std::exception&) {
        return 0;
    }
}

// Safe conversion from string to uint32_t with fallback
static uint32_t safe_stoul(const std::string& s) {
    if (s.empty()) return 0;
    try {
        if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
            return static_cast<uint32_t>(std::stoul(s.substr(2), nullptr, 16));
        }
        return static_cast<uint32_t>(std::stoul(s, nullptr, 0));
    } catch (const std::exception&) {
        return 0;
    }
}

std::vector<WatchDescriptor> InotifyParser::parse_fdinfo(const std::string& path) {
    std::vector<WatchDescriptor> watches;
    std::ifstream file(path);
    if (!file.is_open()) return watches;

    std::string line;
    // More precise regex: ino/sdev/mask can be decimal or hex with 0x prefix
    static const std::regex inotify_regex(
        R"(inotify\s+wd:(\d+)\s+ino:([0-9a-fA-Fx]+)\s+sdev:([0-9a-fA-Fx]+)\s+mask:([0-9a-fA-Fx]+))",
        std::regex::icase
    );

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, inotify_regex) && match.size() == 5) {
            WatchDescriptor wd;
            wd.wd    = std::stoi(match[1].str());  // wd is always decimal
            wd.inode = safe_stoull(match[2].str());
            wd.sdev  = safe_stoull(match[3].str());
            wd.mask  = safe_stoul(match[4].str());
            watches.push_back(wd);
        }
    }
    return watches;
}