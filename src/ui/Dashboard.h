#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "ftxui/component/component.hpp"
#include "ProcessTable.h"
#include "WatchDetails.h"
#include "HistoryTracker.h"
#include "../system/SystemLimits.h"
#include "../scanner/WatchCollector.h"
#include "../scanner/LeakDetector.h"

// Main screen. Every method must be called from the UI thread only.
class Dashboard {
    public:
        Dashboard(std::function<void()> on_quit, std::function<void()> on_refresh);

        ftxui::Component GetComponent();

        // Receives freshly collected data (called on the UI thread via screen.Post).
        void ApplyData(const InotifyLimits& limits, std::vector<ProcessWatchInfo> data);

    private:
        // One line of the "Top consumers" list.
        struct TopEntry {
            int         pid;
            std::string name;
            size_t      count;
        };

        void ApplyFilter();               // rebuilds the visible rows from the search text
        void FocusSearch();
        void FocusTable();
        bool SearchFocused() const;

        ftxui::Element RefreshStatus() const;   // "Refreshing..." / "Refreshed!" / auto-refresh note
        ftxui::Element TopConsumers() const;    // top 3 processes + leak summary

        std::function<void()> on_quit_;
        std::function<void()> on_refresh_;
        bool is_root_;

        InotifyLimits limits_;
        std::vector<ProcessWatchInfo>        all_processes_;   // the only copy of the data
        std::vector<const ProcessWatchInfo*> visible_;         // rows that match the search
        std::optional<int> selected_pid_;

        // Summary of the newest data.
        std::vector<TopEntry> top_;
        std::string           leaking_;                  // names of leaking processes, "" if none

        // Refresh feedback.
        bool        manual_refresh_pending_ = false;     // "r" pressed, waiting for new data
        std::chrono::steady_clock::time_point manual_flash_until_{};
        std::string last_refresh_ = "-";                 // time of the last data update
        uint64_t    refresh_count_ = 0;

        std::unique_ptr<ProcessTable> process_table_;
        std::unique_ptr<WatchDetails> watch_details_;

        ftxui::Component dashboard_container_;
        ftxui::Component main_container_;
        ftxui::Component system_panel_;
        ftxui::Component search_input_;
        std::string      search_filter_;

        LeakDetector   leak_detector_;
        HistoryTracker history_tracker_{60};
};