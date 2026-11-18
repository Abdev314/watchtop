#pragma once
#include <cstdint>
#include <string>
#include <vector>

// One inotify watch, as listed in /proc/<pid>/fdinfo/<fd>.
struct WatchDescriptor {
    int      fd    = -1;   // the inotify file descriptor that owns this watch
    int      wd    = -1;   // watch id inside that inotify instance
    uint64_t inode = 0;    // inode of the watched file or directory
    uint64_t sdev  = 0;    // device that holds the inode
    uint32_t mask  = 0;    // event mask the watch listens for
};

class InotifyParser {
    public:
        // Reads one fdinfo file and returns all "inotify wd:" entries in it.
        static std::vector<WatchDescriptor> parse_fdinfo(const std::string& path);
};