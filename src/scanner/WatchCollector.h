#pragma once
#include <string>
#include <vector>
#include "ProcScanner.h"
#include "InotifyParser.h"

// Everything WatchTop knows about one process that uses inotify.
struct ProcessWatchInfo {
    int         pid = 0;
    std::string name;
    size_t      watch_count = 0;
    std::vector<WatchDescriptor> watches;
    std::vector<std::string>     listening_ports;
    bool        is_leaking = false;
    double      leak_rate  = 0.0;
    uint64_t    start_ticks = 0;   // process start time, used to identify it safely when stopping it
};

class WatchCollector {
    public:
        // Scans /proc and returns only processes that own at least one inotify watch.
        static std::vector<ProcessWatchInfo> collect();
};