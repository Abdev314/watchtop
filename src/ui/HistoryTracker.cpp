#include "HistoryTracker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace ftxui;

namespace {

constexpr int      kGraphRows = 5;    // graph height in text rows
constexpr uint64_t kMinSpan   = 10;   // smallest scale height, so tiny changes stay tiny

// Number to text with a fixed count of decimals, e.g. num(0.094, 2) -> "0.09".
std::string num(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// Seconds to a short text: "42s", "7m", "2h 10m".
std::string format_duration(double s) {
    if (s < 60)    return num(s, 0) + "s";
    if (s < 3600)  return num(std::floor(s / 60), 0) + "m";
    if (s < 86400) {
        const int h = static_cast<int>(s / 3600);
        const int m = static_cast<int>((s - h * 3600) / 60);
        return std::to_string(h) + "h " + std::to_string(m) + "m";
    }
    return ">24h";
}

// Signed number with an explicit "+" for growth, e.g. "+12", "-5", "0".
std::string signed_num(int64_t v) {
    return (v > 0 ? "+" : "") + std::to_string(v);
}

} 

HistoryTracker::HistoryTracker(size_t max_points) : max_points_(max_points) {
    history_.reserve(max_points_ + 1);
}

// Stores one sample. Samples closer than 0.5 s are ignored, so pressing "r"
// does not add an extra bar and one graph column always equals one second.
void HistoryTracker::add_sample(uint64_t count) {
    const auto now = std::chrono::steady_clock::now();
    if (!history_.empty() && now - history_.back().time < std::chrono::milliseconds(500)) {
        history_.back().value = count;   // just update the newest value
        return;
    }
    history_.push_back({now, count});
    if (history_.size() > max_points_)
        history_.erase(history_.begin());
}

Element HistoryTracker::render(uint64_t limit) const {
    if (history_.size() < 2)
        return vbox({text("Watch History") | bold, text("Collecting data...") | dim});

    // Statistics over the stored window.
    const uint64_t now_v = history_.back().value;
    uint64_t lo = now_v, hi = now_v;
    double sum = 0;
    for (const auto& s : history_) {
        lo = std::min(lo, s.value);
        hi = std::max(hi, s.value);
        sum += static_cast<double>(s.value);
    }
    const double avg = sum / static_cast<double>(history_.size());
    const double seconds =
        std::chrono::duration<double>(history_.back().time - history_.front().time).count();

    // Graph scale: bottom = lowest value, top = at least 10 higher.
    const uint64_t base = lo;
    const uint64_t span = std::max<uint64_t>(hi - lo, kMinSpan);
    const uint64_t top  = base + span;

    // Height of each sample in 1/8 text rows (0..kGraphRows*8). Every sample gets at
    // least 1/8 so the line never disappears.
    // The graph always has max_points_ columns; unused columns on the LEFT stay empty,
    // so new data enters from the right and moves left.
    const size_t cols = max_points_;
    const size_t pad  = cols - history_.size();
    std::vector<int> height(cols, 0);
    for (size_t i = 0; i < history_.size(); ++i) {
        const double ratio = static_cast<double>(history_[i].value - base) / static_cast<double>(span);
        height[pad + i] = std::max(1, static_cast<int>(std::lround(ratio * kGraphRows * 8)));
    }

    // Draw from the top row to the bottom row. Each cell is a block from " " to "█".
    static const char* kBlocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    Elements graph_rows;
    for (int row = kGraphRows - 1; row >= 0; --row) {
        std::string line;
        for (size_t c = 0; c + 1 < cols; ++c)
            line += kBlocks[std::clamp(height[c] - row * 8, 0, 8)];
        const char* newest = kBlocks[std::clamp(height[cols - 1] - row * 8, 0, 8)];

        // Value label on the left: top row = top value, bottom row = bottom value.
        std::string label;
        if (row == kGraphRows - 1) label = std::to_string(top);
        else if (row == 0)         label = std::to_string(base);

        graph_rows.push_back(hbox({
            text(label) | align_right | size(WIDTH, EQUAL, 8) | dim,
            text(" │") | dim,
            text(line) | color(Color::Cyan),
            text(newest) | color(Color::Yellow) | bold,      // newest value, always at the right edge
        }));
    }

    // Time axis under the graph.
    const std::string left_time = "-" + format_duration(static_cast<double>(cols));
    graph_rows.push_back(hbox({
        text("") | size(WIDTH, EQUAL, 10),
        text(left_time) | dim,
        filler(),
        text("now") | dim | bold,
    }));

    // Trend: change between the oldest and newest sample.
    const int64_t delta = static_cast<int64_t>(now_v) - static_cast<int64_t>(history_.front().value);
    const double per_sec = seconds > 0 ? static_cast<double>(delta) / seconds : 0.0;

    std::string trend;
    Color trend_color = Color::White;
    if (delta == 0) {
        trend = "→ Stable (no change)";
    } else if (delta > 0) {
        trend = "▲ Growing " + signed_num(delta) + " in " + format_duration(seconds) +
                " (" + num(per_sec * 60.0, 1) + "/min)";
        trend_color = Color::Yellow;
    } else {
        trend = "▼ Dropping " + signed_num(delta) + " in " + format_duration(seconds);
        trend_color = Color::Green;
    }

    const std::string of_limit =
        limit > 0 ? " (" + num(100.0 * static_cast<double>(now_v) / static_cast<double>(limit), 2) + "% of limit)"
                  : "";

    Elements rows;
    rows.push_back(text("Watch History: total watches over time (1 column = 1 second)") | bold);
    for (auto& g : graph_rows) rows.push_back(std::move(g));
    rows.push_back(hbox({text("Now: "), text(std::to_string(now_v)) | bold, text(of_limit) | dim}));
    rows.push_back(text("Min: " + std::to_string(lo) + "   Avg: " + num(avg, 0) +
                        "   Max: " + std::to_string(hi)));
    rows.push_back(text(trend) | color(trend_color));

    // Forecast: only when growing and a limit is known.
    if (per_sec > 0.01 && limit > now_v) {
        const double left = static_cast<double>(limit - now_v) / per_sec;
        rows.push_back(text("Limit reached in ~" + format_duration(left) + " at this rate") |
                       color(left < 600 ? Color::Red : Color::White));
    }
    return vbox(std::move(rows));
}