#pragma once
#include <unordered_map>
#include <deque>
#include <chrono>
#include "WatchCollector.h"   // defines ProcessWatchInfo

struct LeakInfo {
    bool is_leaking = false;
    double leak_rate = 0.0;
    int current_count = 0;
};

class LeakDetector {
public:
    // FIXED: parameter type should be ProcessWatchInfo, not WatchCollector
    std::unordered_map<int, LeakInfo> update(const std::vector<ProcessWatchInfo>& processes);

private:
    struct HistoryEntry {
        std::chrono::steady_clock::time_point timestamp;
        int watch_count;
    };
    std::unordered_map<int, std::deque<HistoryEntry>> history_;
    
    static constexpr int MAX_HISTORY = 10;
    static constexpr double LEAK_THRESHOLD = 1.0;
    static constexpr int MIN_SAMPLES = 3;
};