#pragma once
#include <memory>
#include <optional>
#include "ftxui/component/component.hpp"
#include "ProcessTable.h"
#include "WatchDetails.h"
#include "CommandPanel.h"
#include "../system/SystemLimits.h"
#include "../scanner/WatchCollector.h"
#include "../scanner/LeakDetector.h"

class Dashboard {
public:
    Dashboard();
    ftxui::Component GetComponent();
    void RefreshData();
    

private:
    InotifyLimits limits_;
    std::vector<ProcessWatchInfo> processes_;
    std::optional<int> selected_pid_;

    std::unique_ptr<ProcessTable> process_table_;
    std::unique_ptr<WatchDetails> watch_details_;
    std::unique_ptr<CommandPanel> command_panel_;
    ftxui::Component dashboard_container_;
    ftxui::Component main_container_;
    ftxui::Component system_panel_;
    
    std::string search_filter_;
    ftxui::Component search_input_;

    LeakDetector leak_detector_;

};