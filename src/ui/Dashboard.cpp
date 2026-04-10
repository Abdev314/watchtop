#include "Dashboard.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/component.hpp"
#include <algorithm>
#include <chrono>  
#include <ctime>    

using namespace ftxui;

Dashboard::Dashboard() {
    watch_details_ = std::make_unique<WatchDetails>();
    command_panel_ = std::make_unique<CommandPanel>();
    search_input_  = Input(&search_filter_, "Search by PID, name or port...");

    process_table_ = std::make_unique<ProcessTable>(
        [this](const ProcessWatchInfo& info) {
            selected_pid_ = info.pid;
            watch_details_->SetProcess(info);
        });

    system_panel_ = Renderer([&] {
        double usage = limits_.usage_percent();
        std::string bar;
        int filled = static_cast<int>(usage / 10);
        for (int i = 0; i < 10; ++i)
            bar += (i < filled) ? '#' : ' ';
        std::string usage_bar = "[" + bar + "]";

        Elements rows;
        rows.push_back(text("System Watch Status") | bold);
        rows.push_back(separator());
        rows.push_back(hbox({text("Max User Watches:  "), text(std::to_string(limits_.max_user_watches)) | dim}));
        rows.push_back(hbox({text("Max User Instances:"), text(std::to_string(limits_.max_user_instances)) | dim}));
        rows.push_back(hbox({text("Max Queued Events: "), text(std::to_string(limits_.max_queued_events)) | dim}));
        rows.push_back(separator());
        rows.push_back(hbox({text("Active Watches:    "), text(std::to_string(limits_.current_watches)) | color(usage > 80 ? Color::Red : Color::White)}));
        rows.push_back(hbox({text("Usage:             "), text(std::to_string(static_cast<int>(usage)) + "% " + usage_bar) |
                                                       color(usage > 80 ? Color::Red : Color::White)}));
        if (usage > 80)
            rows.push_back(text("WARNING: Watch usage above 80%") | color(Color::Red) | bold);

        rows.push_back(separator());
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&now_c);
        if (!time_str.empty() && time_str.back() == '\n')
            time_str.pop_back();
        rows.push_back(text("Last refresh: " + time_str) | dim);

        return vbox(std::move(rows)) | border;
    });

    dashboard_container_ = Container::Vertical({
        search_input_,
        process_table_->GetComponent(),
        watch_details_->GetComponent(),
        command_panel_->GetComponent()
    });

    dashboard_container_->SetActiveChild(process_table_->GetComponent());

    main_container_ = Renderer(dashboard_container_, [&] {
        return vbox({
            text(" WATCHTOP DASHBOARD ") | bold | center | border,
            system_panel_->Render(),
            hbox({
                text(" 🔍 "),
                search_input_->Render() | flex
            }) | border,
            hbox({
                process_table_->GetComponent()->Render() | flex | size(WIDTH, GREATER_THAN, 45),
                separator(),
                watch_details_->GetComponent()->Render() | flex | size(WIDTH, GREATER_THAN, 40),
            }) | flex,
            command_panel_->GetComponent()->Render() | size(HEIGHT, EQUAL, 15),
            text("↑↓: Navigate  ↵: Inspect  Tab: Switch Focus  /: Search  r: Refresh  q: Quit") | dim | center
        }) | size(HEIGHT, EQUAL, Terminal::Size().dimy);
    });

    main_container_ |= CatchEvent([&](Event event) {
        if (event == Event::Tab) {
            auto active = dashboard_container_->ActiveChild();
            if (active == search_input_) {
                command_panel_->Unfocus();
                dashboard_container_->SetActiveChild(process_table_->GetComponent());
                process_table_->GetComponent()->TakeFocus();
            } else if (active == process_table_->GetComponent()) {
                command_panel_->Unfocus();
                dashboard_container_->SetActiveChild(command_panel_->GetComponent());
                command_panel_->Focus();
            } else {
                // leaving command panel → go to search
                command_panel_->Unfocus();
                dashboard_container_->SetActiveChild(search_input_);
                search_input_->TakeFocus();
            }
            return true;
        }
        if (event == Event::Character('/')) {
            command_panel_->Unfocus();
            dashboard_container_->SetActiveChild(search_input_);
            search_input_->TakeFocus();
            return true;
        }
        return false;
    });

    RefreshData();
}

ftxui::Component Dashboard::GetComponent() { return main_container_; }

void Dashboard::RefreshData() {
    limits_      = SystemLimits::read();
    auto allProcesses = WatchCollector::collect();

    std::string lowerFilter = search_filter_;
    std::transform(lowerFilter.begin(), lowerFilter.end(),
                   lowerFilter.begin(), ::tolower);

    std::vector<ProcessWatchInfo> filtered;
    for (const auto& p : allProcesses) {
        if (lowerFilter.empty()) {
            filtered.push_back(p);
            continue;
        }

        if (std::to_string(p.pid).find(lowerFilter) != std::string::npos) {
            filtered.push_back(p);
            continue;
        }

        // Match process name
        std::string lowerName = p.name;
        std::transform(lowerName.begin(), lowerName.end(),
                       lowerName.begin(), ::tolower);
        if (lowerName.find(lowerFilter) != std::string::npos) {
            filtered.push_back(p);
            continue;
        }

        bool portMatch = false;
        for (const auto& port : p.listening_ports) {
            std::string lowerPort = port;
            std::transform(lowerPort.begin(), lowerPort.end(),
                           lowerPort.begin(), ::tolower);
            if (lowerPort.find(lowerFilter) != std::string::npos) {
                portMatch = true;
                break;
            }
        }
        if (portMatch) {
            filtered.push_back(p);
            continue;
        }
    }

    processes_ = std::move(filtered);

    uint64_t total = 0;
    for (const auto& p : processes_)
        total += p.watch_count;
    limits_.current_watches = total;

    process_table_->UpdateData(processes_);

    if (!selected_pid_.has_value()) {
        watch_details_->Clear();
        return;
    }

    auto it = std::find_if(processes_.begin(), processes_.end(),
        [&](const ProcessWatchInfo& p) { return p.pid == *selected_pid_; });

    if (it == processes_.end()) {
        selected_pid_.reset();
        watch_details_->Clear();
    } else {
        watch_details_->SetProcess(*it);
    }
}