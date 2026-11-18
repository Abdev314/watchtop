#pragma once
#include <functional>
#include <optional>
#include <vector>
#include "ftxui/component/component.hpp"
#include "../scanner/WatchCollector.h"

class ProcessTableComponent;

// Left panel: a scrollable table of processes. It stores pointers to the
// Dashboard's data instead of copying it, so a refresh costs almost nothing.
class ProcessTable {
    public:
        using SelectionCallback = std::function<void(const ProcessWatchInfo&)>;

        explicit ProcessTable(SelectionCallback on_select);
        ftxui::Component GetComponent();

        // `rows` must stay valid until the next UpdateData call.
        void UpdateData(std::vector<const ProcessWatchInfo*> rows);
        std::optional<int> SelectedPid() const;

    private:
        friend class ProcessTableComponent;

        std::vector<const ProcessWatchInfo*> data_;
        int                selected_index_ = -1;
        std::optional<int> selected_pid_;     // remembered so the selection survives refreshes
        SelectionCallback  on_select_;
        ftxui::Component   table_component_;

        bool MoveSelection(int delta);
        bool ActivateSelection();
};