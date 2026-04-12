#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "ftxui/dom/elements.hpp"

class HistoryTracker {
public:
    explicit HistoryTracker(size_t max_points = 60);

    void add_sample(uint64_t count);
    ftxui::Element render(uint64_t max_value) const;

private:
    std::vector<uint64_t> history_;
    size_t max_points_;
};