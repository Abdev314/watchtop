#include "WatchDetails.h"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

WatchDetails::WatchDetails() {
    component_ = Renderer([&] {
        if (!current_process_.has_value()) {
            return text("No process selected") | center | flex;
        }

        const auto& proc = current_process_.value();
        Elements rows;
        rows.push_back(text("Process: " + proc.name + " (PID: " + std::to_string(proc.pid) + ")") | bold);
        rows.push_back(separator());
        rows.push_back(text("Watch Descriptors") | bold);
        rows.push_back(text("WD    Path"));

        for (const auto& wd : proc.watches) {
            std::string path = "unknown";
            auto it = proc.wd_to_path.find(wd.wd);
            if (it != proc.wd_to_path.end())
                path = it->second;

            rows.push_back(text(std::to_string(wd.wd) + "     " + path));
        }

        return vbox(std::move(rows)) | border | flex;
    });
}

ftxui::Component WatchDetails::GetComponent() { return component_; }

void WatchDetails::SetProcess(std::optional<ProcessWatchInfo> info) {
    current_process_ = std::move(info);
}

void WatchDetails::Clear() { current_process_.reset(); }