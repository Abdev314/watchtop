#pragma once
#include "ftxui/component/component.hpp"
#include "../scanner/WatchCollector.h"

// Right panel: watch list of the selected process.
class WatchDetails {
    public:
        WatchDetails();
        ftxui::Component GetComponent();

        // `info` points into the Dashboard's data and must stay valid until the next call.
        void SetProcess(const ProcessWatchInfo* info);
        void Clear();

    private:
        const ProcessWatchInfo* current_ = nullptr;
        ftxui::Component component_;
};