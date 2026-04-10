#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "ProcScanner.h"
#include "InotifyParser.h"

struct ProcessWatchInfo {
    int pid;
    std::string name;
    size_t watch_count = 0;
    std::vector<WatchDescriptor> watches;      // raw watch data
    std::unordered_map<int, std::string> wd_to_path; // resolved path for each wd
    std::vector<std::string> listening_ports;   

};

class WatchCollector {
public:
    // Collects watch information for all processes
    static std::vector<ProcessWatchInfo> collect();
};