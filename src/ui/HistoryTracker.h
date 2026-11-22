#pragma once
#include <chrono>
#include <cstdint>
#include <vector>
#include "ftxui/dom/elements.hpp"

// Keeps the last N samples of the total watch count and draws a summary panel:
// graph, current value, min/avg/max, trend, and time left until the limit.
class HistoryTracker {
    public:
        explicit HistoryTracker(size_t max_points = 60);

        void add_sample(uint64_t count);

        // Seconds between samples (the refresh interval). Used for the graph labels
        // and for merging samples that arrive too close together.
        void set_interval(int seconds);

        // `limit` is max_user_watches (0 = unknown).
        ftxui::Element render(uint64_t limit) const;

    private:
        struct Sample {
            std::chrono::steady_clock::time_point time;
            uint64_t value;
        };
        std::vector<Sample> history_;
        size_t max_points_;
        int interval_ = 1;
};