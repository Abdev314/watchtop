#pragma once
#include <vector>
#include <functional>
#include "ftxui/component/component.hpp"
#include "../scanner/WatchCollector.h"

class ProcessTable {
public:
    using SelectionCallback = std::function<void(const ProcessWatchInfo&)>;

    explicit ProcessTable(SelectionCallback on_select);
    ftxui::Component GetComponent();
    void UpdateData(std::vector<ProcessWatchInfo> data);

private:
    std::vector<ProcessWatchInfo> data_;
    int selected_index_ = 0;
    SelectionCallback on_select_;
    ftxui::Component table_component_;
};