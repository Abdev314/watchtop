#include "LeakDetector.h"
#include <algorithm>
#include <unordered_set>

std::unordered_map<int, LeakInfo> LeakDetector::update(const std::vector<ProcessWatchInfo>& processes){
    std::unordered_map<int, LeakInfo> result;
    auto now = std::chrono::steady_clock::now();
    
    // Build a set of current PIDs for cleanup
    std::unordered_set<int> current_pids;
    for (const auto& p : processes) {
        current_pids.insert(p.pid);
    }
    
    // Remove history for dead processes
    for (auto it = history_.begin(); it != history_.end(); ) {
        if (current_pids.count(it->first) == 0) {
            it = history_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update history for each live process
    for (const auto& p : processes) {
        auto& hist = history_[p.pid];
        hist.push_back({now, static_cast<int>(p.watch_count)});
        if (hist.size() > MAX_HISTORY) {
            hist.pop_front();
        }
        
        LeakInfo info;
        info.current_count = p.watch_count;
        
        // Need enough samples to detect leak
        if (hist.size() >= MIN_SAMPLES) {
            const auto& oldest = hist.front();
            const auto& newest = hist.back();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                newest.timestamp - oldest.timestamp).count() / 1000.0; // seconds
            
            if (duration > 0.1) { // avoid division by zero
                int delta = newest.watch_count - oldest.watch_count;
                info.leak_rate = delta / duration;
                info.is_leaking = (info.leak_rate > LEAK_THRESHOLD);
            }
        }
        result[p.pid] = info;
    }
    return result;
}