#pragma once
#include "ftxui/component/component.hpp"
#include "ProcessTable.h"
#include "WatchDetails.h"
#include "../system/SystemLimits.h"
#include "../scanner/WatchCollector.h"

class Dashboard {
public:
    Dashboard();
    ftxui::Component GetComponent();
    void RefreshData();

private:
    InotifyLimits limits_;
    std::vector<ProcessWatchInfo> processes_;

    std::unique_ptr<ProcessTable> process_table_;
    std::unique_ptr<WatchDetails> watch_details_;
    ftxui::Component main_container_;
    ftxui::Component system_panel_;
    ftxui::Component layout_;
};