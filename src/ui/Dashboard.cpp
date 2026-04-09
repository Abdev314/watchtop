#include "Dashboard.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/component.hpp"
#include <memory>

using namespace ftxui;

Dashboard::Dashboard() {
    process_table_ = std::make_unique<ProcessTable>(
        [this](const ProcessWatchInfo& info) {
            watch_details_->SetProcess(info);
        }
    );

    watch_details_ = std::make_unique<WatchDetails>();

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

    // Create a container with the process table as the active, focusable child
    auto container = Container::Vertical({
        system_panel_,
        process_table_->GetComponent(),
        watch_details_->GetComponent()
    });

    // Set the process table as the focused child
    container->SetActiveChild(process_table_->GetComponent());

    // Main renderer
    main_container_ = Renderer(container, [&] {
        return vbox({
            text(" WATCHTOP DASHBOARD ") | bold | center | border,
            system_panel_->Render() | flex,
            process_table_->GetComponent()->Render() | flex,
            watch_details_->GetComponent()->Render() | flex,
            text("↑↓: Navigate  ↵: Inspect  r: Refresh  q: Quit") | dim | center
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
    watch_details_->Clear();
}