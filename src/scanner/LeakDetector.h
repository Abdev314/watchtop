#pragma once
#include <chrono>
#include <deque>
#include <unordered_map>
#include <vector>
#include "WatchCollector.h"

struct LeakInfo {
    bool   is_leaking   = false;
    double leak_rate    = 0.0;   // watches gained per second
    int    current_count = 0;
};

// Flags processes whose watch count keeps growing steadily.
class LeakDetector {
    public:
        // Call once per refresh with ALL processes (never a filtered list).
        std::unordered_map<int, LeakInfo> update(const std::vector<ProcessWatchInfo>& processes);

    private:
        struct HistoryEntry {
            std::chrono::steady_clock::time_point timestamp;
            int watch_count;
        };
        std::unordered_map<int, std::deque<HistoryEntry>> history_;

        static constexpr size_t MAX_HISTORY    = 30;    // samples kept per process
        static constexpr size_t MIN_SAMPLES    = 10;    // samples needed before judging
        static constexpr double LEAK_THRESHOLD = 2.0;   // watches per second
};