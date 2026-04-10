#pragma once
#include <vector>
#include <functional>
#include <optional>
#include "ftxui/component/component.hpp"
#include "../scanner/WatchCollector.h"

class ProcessTableComponent;  // forward declaration

class ProcessTable {
public:
    using SelectionCallback = std::function<void(const ProcessWatchInfo&)>;

    explicit ProcessTable(SelectionCallback on_select);
    ftxui::Component GetComponent();
    void UpdateData(std::vector<ProcessWatchInfo> data);
    std::optional<int> SelectedPid() const;

private:
    friend class ProcessTableComponent;  

    std::vector<ProcessWatchInfo> data_;
    int selected_index_ = -1;
    SelectionCallback on_select_;
    ftxui::Component table_component_;

    bool MoveSelection(int delta);
    bool ActivateSelection();
};