#include "Dashboard.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/component.hpp"
#include <algorithm>

using namespace ftxui;

Dashboard::Dashboard() {
    watch_details_ = std::make_unique<WatchDetails>();
    command_panel_ = std::make_unique<CommandPanel>();

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
        return vbox(std::move(rows)) | border;
    });

    dashboard_container_ = Container::Vertical({
        system_panel_,
        process_table_->GetComponent(),
        watch_details_->GetComponent(),
        command_panel_->GetComponent()
    });

    dashboard_container_->SetActiveChild(process_table_->GetComponent());

    dashboard_container_ |= CatchEvent([&](Event event) {
        if (event == Event::Tab) {
            if (dashboard_container_->ActiveChild() == process_table_->GetComponent()) {
                dashboard_container_->SetActiveChild(command_panel_->GetComponent());
                command_panel_->Focus();
            } else {
                dashboard_container_->SetActiveChild(process_table_->GetComponent());
            }
            return true;
        }
        return false;
    });

    main_container_ = Renderer(dashboard_container_, [&] {
        return vbox({
            text(" WATCHTOP DASHBOARD ") | bold | center | border,
            system_panel_->Render() | flex,
            process_table_->GetComponent()->Render() | flex,
            watch_details_->GetComponent()->Render() | flex,
            command_panel_->GetComponent()->Render() | size(HEIGHT, EQUAL, 15),
            text("↑↓: Navigate Table  ↵: Inspect  Tab: Toggle Panel  r: Refresh  q: Quit") | dim | center
        }) | size(HEIGHT, EQUAL, Terminal::Size().dimy);
    });

    RefreshData();
}

ftxui::Component Dashboard::GetComponent() { return main_container_; }

void Dashboard::RefreshData() {
    limits_ = SystemLimits::read();
    processes_ = WatchCollector::collect();

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