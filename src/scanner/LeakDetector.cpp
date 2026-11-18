#include "LeakDetector.h"
#include <unordered_set>

// Keeps a short history per process and marks it as leaking when the count
// grew fast AND never went down during the window.
std::unordered_map<int, LeakInfo>
LeakDetector::update(const std::vector<ProcessWatchInfo>& processes) {
    std::unordered_map<int, LeakInfo> result;
    const auto now = std::chrono::steady_clock::now();

    // Forget processes that no longer exist.
    std::unordered_set<int> alive;
    for (const auto& p : processes) alive.insert(p.pid);
    for (auto it = history_.begin(); it != history_.end();)
        it = alive.count(it->first) ? std::next(it) : history_.erase(it);

    for (const auto& p : processes) {
        auto& hist = history_[p.pid];
        hist.push_back({now, static_cast<int>(p.watch_count)});
        if (hist.size() > MAX_HISTORY) hist.pop_front();

        LeakInfo info;
        info.current_count = static_cast<int>(p.watch_count);

        if (hist.size() >= MIN_SAMPLES) {
            const double seconds = std::chrono::duration<double>(
                hist.back().timestamp - hist.front().timestamp).count();

            if (seconds > 1.0) {
                info.leak_rate = (hist.back().watch_count - hist.front().watch_count) / seconds;

                // A real leak only goes up. Normal fluctuation goes up and down.
                bool never_dropped = true;
                for (size_t i = 1; i < hist.size(); ++i)
                    if (hist[i].watch_count < hist[i - 1].watch_count) never_dropped = false;

                info.is_leaking = never_dropped && info.leak_rate > LEAK_THRESHOLD;
            }
        }
        result[p.pid] = info;
    }
    return result;
}