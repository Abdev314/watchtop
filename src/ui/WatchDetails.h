#pragma once
#include <optional>
#include "ftxui/component/component.hpp"
#include "../scanner/WatchCollector.h"

class WatchDetails {
public:
    WatchDetails();  
    ftxui::Component GetComponent();
    void SetProcess(std::optional<ProcessWatchInfo> info);
    void Clear();

private:
    std::optional<ProcessWatchInfo> current_process_;
    ftxui::Component component_;
};