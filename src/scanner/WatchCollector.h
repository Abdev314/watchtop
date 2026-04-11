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
    std::vector<WatchDescriptor> watches;      
    std::unordered_map<int, std::string> wd_to_path; 
    std::vector<std::string> listening_ports;   
    bool is_leaking = false;
    double leak_rate = 0.0;

};

class WatchCollector {
public:
    static std::vector<ProcessWatchInfo> collect();
};