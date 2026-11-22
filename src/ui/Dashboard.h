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
#include "../system/ProcessControl.h"
#include "../system/SystemLimits.h"
#include "../scanner/WatchCollector.h"
#include "../scanner/LeakDetector.h"

// Main screen. Every method must be called from the UI thread only.
class Dashboard {
    public:
        Dashboard(std::function<void()> on_quit, std::function<void()> on_refresh, int refresh_seconds);

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

        // A copy of the process the user wants to stop. It is a COPY on purpose:
        // the live data is replaced every second while the dialog is open.
        struct PendingKill {
            int         pid;
            std::string name;
            std::string ports;
            size_t      watches;
            uint64_t    start_ticks;
        };

        void ApplyFilter();               // rebuilds the visible rows from the search text
        void FocusSearch();
        void FocusTable();
        bool SearchFocused() const;

        void AskToKill();                         // "k": open the confirmation dialog
        void ConfirmKill(KillSignal signal);      // Enter / y / f inside the dialog
        void SetMessage(std::string text, bool is_error);

        ftxui::Element RefreshStatus() const;   // "Refreshing..." / "Refreshed!" / auto-refresh note
        ftxui::Element TopConsumers() const;    // top 3 processes + leak summary
        ftxui::Element KillDialog() const;      // the confirmation box
        ftxui::Element FooterHint() const;      // key help, or the result of the last action

        std::function<void()> on_quit_;
        std::function<void()> on_refresh_;
        bool is_root_;
        int  refresh_seconds_;   // seconds between automatic refreshes

        InotifyLimits limits_;
        std::vector<ProcessWatchInfo>        all_processes_;   // the only copy of the data
        std::vector<const ProcessWatchInfo*> visible_;         // rows that match the search
        std::optional<int> selected_pid_;

        // Summary of the newest data.
        std::vector<TopEntry> top_;
        std::string           leaking_;                  // "name (pid)" of leaking processes, "" if none

        // Refresh feedback.
        bool        manual_refresh_pending_ = false;     // "r" pressed, waiting for new data
        std::chrono::steady_clock::time_point manual_flash_until_{};
        std::string last_refresh_ = "-";                 // time of the last data update
        uint64_t    refresh_count_ = 0;

        // Kill feature.
        std::optional<PendingKill> pending_kill_;        // set while the dialog is open
        std::string action_message_;                     // result of the last action
        bool        action_is_error_ = false;
        std::chrono::steady_clock::time_point action_until_{};

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