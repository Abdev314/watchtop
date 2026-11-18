#include "ProcessTable.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include <algorithm>

using namespace ftxui;

static constexpr int kVisibleRows = 15;

class ProcessTableComponent : public ComponentBase {
public:
    explicit ProcessTableComponent(ProcessTable* parent) : parent_(parent) {}

    // Draws a window of kVisibleRows rows around the selected row.
    Element Render() override {
        if (parent_->data_.empty())
            return text(" No processes with inotify watches found ") | center | border;

        const auto& data  = parent_->data_;
        const int   total = static_cast<int>(data.size());
        const int   sel   = std::max(0, parent_->selected_index_);

        int win_start = std::max(0, sel - kVisibleRows / 2);
        int win_end   = std::min(total, win_start + kVisibleRows);
        win_start     = std::max(0, win_end - kVisibleRows);

        std::vector<std::vector<std::string>> rows;
        rows.push_back({"PID", "", "Process", "Watches", "Ports"});

        for (int i = win_start; i < win_end; ++i) {
            const ProcessWatchInfo& p = *data[i];

            std::string ports;
            for (size_t j = 0; j < p.listening_ports.size(); ++j) {
                if (j > 0) ports += ", ";
                ports += p.listening_ports[j];
            }
            if (ports.empty()) ports = "-";

            rows.push_back({std::to_string(p.pid), "", p.name,
                            std::to_string(p.watch_count), ports});
        }

        auto table = Table(rows);
        table.SelectAll().Border(LIGHT);
        table.SelectRow(0).Decorate(bold);
        table.SelectColumn(0).DecorateCells(align_right | size(WIDTH, EQUAL, 7));
        table.SelectColumn(1).DecorateCells(size(WIDTH, EQUAL, 2));
        table.SelectColumn(2).DecorateCells(size(WIDTH, GREATER_THAN, 30));
        table.SelectColumn(3).DecorateCells(align_right | size(WIDTH, EQUAL, 7));
        table.SelectColumn(4).DecorateCells(align_right | size(WIDTH, EQUAL, 12));

        // Selected row = cyan background. Leaking rows = yellow text.
        for (int i = win_start; i < win_end; ++i) {
            const int row = (i - win_start) + 1;   // +1 because row 0 is the header
            if (i == sel)
                table.SelectRow(row).DecorateCells(bgcolor(Color::Cyan) | color(Color::Black));
            else if (data[i]->is_leaking)
                table.SelectRow(row).DecorateCells(color(Color::Yellow));
        }

        const std::string scroll_info = std::to_string(sel + 1) + "/" + std::to_string(total);

        return vbox({
                   table.Render() | yframe | vscroll_indicator,
                   text(" " + scroll_info + " ") | align_right | dim,
               })
               | size(HEIGHT, EQUAL, 30)
               | size(WIDTH, EQUAL, 60);
    }

    bool OnEvent(Event event) override {
        if (event == Event::ArrowUp)   return parent_->MoveSelection(-1);
        if (event == Event::ArrowDown) return parent_->MoveSelection(1);
        if (event == Event::Return)    return parent_->ActivateSelection();
        return false;
    }

    bool Focusable() const override { return true; }

private:
    ProcessTable* parent_;
};

ProcessTable::ProcessTable(SelectionCallback on_select)
    : on_select_(std::move(on_select)) {
    table_component_ = Make<ProcessTableComponent>(this);
}

ftxui::Component ProcessTable::GetComponent() { return table_component_; }

// Replaces the rows. Keeps the same process selected (by PID) if it still exists.
// Note: the old pointers are never read here, because they may already be invalid.
void ProcessTable::UpdateData(std::vector<const ProcessWatchInfo*> rows) {
    data_ = std::move(rows);

    if (data_.empty()) {
        selected_index_ = -1;
        return;
    }

    if (selected_pid_) {
        for (size_t i = 0; i < data_.size(); ++i) {
            if (data_[i]->pid == *selected_pid_) {
                selected_index_ = static_cast<int>(i);
                return;
            }
        }
    }

    selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(data_.size()) - 1);
    selected_pid_   = data_[selected_index_]->pid;
}

bool ProcessTable::MoveSelection(int delta) {
    if (data_.empty()) return true;
    if (selected_index_ < 0) selected_index_ = 0;
    else selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(data_.size()) - 1);
    selected_pid_ = data_[selected_index_]->pid;
    return true;
}

bool ProcessTable::ActivateSelection() {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(data_.size()))
        on_select_(*data_[selected_index_]);
    return true;
}

std::optional<int> ProcessTable::SelectedPid() const {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(data_.size()))
        return data_[selected_index_]->pid;
    return std::nullopt;
}