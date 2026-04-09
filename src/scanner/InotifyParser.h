#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct WatchDescriptor {
    int wd = -1;
    uint64_t inode = 0;
    uint64_t sdev = 0;
    uint32_t mask = 0;
};

class InotifyParser {
public:
    static std::vector<WatchDescriptor> parse_fdinfo(const std::string& path);
};