#include "HistoryTracker.h"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <cmath>

using namespace ftxui;

HistoryTracker::HistoryTracker(size_t max_points)
    : max_points_(max_points) {
    history_.reserve(max_points_);
}

void HistoryTracker::add_sample(uint64_t count) {
    history_.push_back(count);
    if (history_.size() > max_points_)
        history_.erase(history_.begin());
}

Element HistoryTracker::render(uint64_t max_value) const {
    if (history_.empty())
        return text("No data yet") | dim | center;

    // Determine scaling
    uint64_t actual_max = *std::max_element(history_.begin(), history_.end());
    uint64_t actual_min = *std::min_element(history_.begin(), history_.end());
    if (actual_max == 0) actual_max = 1;

    // Build sparkline using block characters
    std::string sparkline;
    sparkline.reserve(history_.size());

    // Unicode block characters from lowest to highest density
    static const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    const int num_blocks = sizeof(blocks) / sizeof(blocks[0]) - 1;

    for (uint64_t val : history_) {
        // Map value to 0..num_blocks
        int level = 0;
        if (actual_max > actual_min) {
            double ratio = static_cast<double>(val - actual_min) /
                           static_cast<double>(actual_max - actual_min);
            level = static_cast<int>(std::round(ratio * num_blocks));
            if (level < 0) level = 0;
            if (level > num_blocks) level = num_blocks;
        }
        sparkline += blocks[level];
    }

    // Create graph element
    return vbox({
        text("Watch History (last " + std::to_string(max_points_) + "s)") | bold,
        hbox({
            text(sparkline) | color(Color::Cyan),
        }) | border | center,
        hbox({
            text(std::to_string(actual_min)) | dim,
            filler(),
            text(std::to_string(actual_max)) | dim,
        })
    });
}