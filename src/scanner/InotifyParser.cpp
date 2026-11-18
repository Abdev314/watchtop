#include "InotifyParser.h"

#include <cstdlib>
#include <fstream>
#include <string_view>

namespace {

// Reads a hexadecimal number that follows `field` (e.g. " ino:") and moves `p` past it.
// An empty `field` just reads a number at the current position.
bool read_hex(const char*& p, std::string_view field, uint64_t& out) {
    if (!std::string_view(p).starts_with(field)) return false;
    p += field.size();

    char* end = nullptr;
    out = std::strtoull(p, &end, 16);
    if (end == p) return false;

    p = end;
    return true;
}

} // namespace

// Parses lines such as:  inotify wd:1f ino:3a2b sdev:803 mask:fc6 ...
// The kernel prints ALL these numbers in hexadecimal.
std::vector<WatchDescriptor> InotifyParser::parse_fdinfo(const std::string& path) {
    std::vector<WatchDescriptor> watches;

    std::ifstream file(path);
    if (!file.is_open()) return watches;   // process exited or access denied

    constexpr std::string_view prefix = "inotify wd:";
    std::string line;

    while (std::getline(file, line)) {
        if (!line.starts_with(prefix)) continue;

        const char* p = line.c_str() + prefix.size();
        uint64_t wd = 0, ino = 0, sdev = 0, mask = 0;

        if (!read_hex(p, "", wd)) continue;   // a line without a watch id is not a watch
        read_hex(p, " ino:",  ino);
        read_hex(p, " sdev:", sdev);
        read_hex(p, " mask:", mask);

        WatchDescriptor w;
        w.wd    = static_cast<int>(wd);
        w.inode = ino;
        w.sdev  = sdev;
        w.mask  = static_cast<uint32_t>(mask);
        watches.push_back(w);
    }
    return watches;
}