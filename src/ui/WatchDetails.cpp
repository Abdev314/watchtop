#include "WatchDetails.h"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

WatchDetails::WatchDetails() {
    component_ = Renderer([&] {
        if (!current_process_.has_value()) {
            return text("No process selected") | center | size(HEIGHT, EQUAL, 20) | border;
        }

        const auto& proc = current_process_.value();
        Elements rows;
        
        // === FIXED HEADER (always visible, never scrolls) ===
        rows.push_back(text("Process: " + proc.name + " (PID: " + std::to_string(proc.pid) + ")") | bold);
        rows.push_back(separator());
        rows.push_back(text("Watch Descriptors") | bold);
        rows.push_back(hbox({
            text("WD") | size(WIDTH, EQUAL, 6) | bold,
            text("Path") | bold | xflex
        }));
        rows.push_back(separator());

        // === SCROLLABLE WATCH LIST ===
        Elements watch_rows;
        for (const auto& wd : proc.watches) {
            std::string path = proc.wd_to_path.count(wd.wd) 
                ? proc.wd_to_path.at(wd.wd) 
                : "unknown";
            
            watch_rows.push_back(hbox({
                text(std::to_string(wd.wd)) | size(WIDTH, EQUAL, 6),
                text(path) | xflex
            }));
        }

        // ✅ CRITICAL: Wrap ONLY the watch list in yframe + height limit
        auto scrollable_content = vbox(std::move(watch_rows)) 
                                | yframe 
                                | vscroll_indicator;

        rows.push_back(scrollable_content | flex);  // flex fills remaining space in parent

        // ✅ CRITICAL: Apply border FIRST, then size constraint
        // This ensures the border stays tight and the container doesn't expand
        return vbox(std::move(rows)) 
             | border 
             | size(HEIGHT, EQUAL, 20)   // ✅ Fixed height - prevents expansion
             | size(WIDTH, EQUAL, 60);   // ✅ Match Dashboard width constraint
    });
}

ftxui::Component WatchDetails::GetComponent() { 
    return component_; 
}

void WatchDetails::SetProcess(std::optional<ProcessWatchInfo> info) {
    current_process_ = std::move(info);
}

void WatchDetails::Clear() { 
    current_process_.reset(); 
}