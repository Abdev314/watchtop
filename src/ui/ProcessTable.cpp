#include "ProcessTable.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"

using namespace ftxui;

static constexpr int kVisibleRows = 15; 

class ProcessTableComponent : public ComponentBase {
public:
    explicit ProcessTableComponent(ProcessTable* parent) : parent_(parent) {}

    Element Render() override {
        if (parent_->data_.empty())
            return text(" No processes with inotify watches found ") | center | border;

        const auto& data     = parent_->data_;
        const int   sel      = parent_->selected_index_;
        const int   total    = static_cast<int>(data.size());

        // ── Compute the visible window ────────────────────────────────────
        int half   = kVisibleRows / 2;
        int win_start = sel - half;
        int win_end   = win_start + kVisibleRows;

        if (win_start < 0) {
            win_start = 0;
            win_end   = std::min(kVisibleRows, total);
        }
        if (win_end > total) {
            win_end   = total;
            win_start = std::max(0, win_end - kVisibleRows);
        }

        // ── Build table content (header + visible slice) ──────────────────
        std::vector<std::vector<std::string>> table_content;
        table_content.push_back({"PID", "", "Process", "Watches", "Ports"});

        for (int i = win_start; i < win_end; ++i) {
            const auto& p = data[i];

            // Join ports into a single comma-separated string
            std::string ports;
            for (size_t j = 0; j < p.listening_ports.size(); ++j) {
                if (j > 0) ports += ", ";
                ports += p.listening_ports[j];
            }
            if (ports.empty()) ports = "-";

            table_content.push_back({
                std::to_string(p.pid),
                "",
                p.name,
                std::to_string(p.watch_count),
                ports
            });
        }

        auto table = Table(table_content);
        table.SelectAll().Border(LIGHT);
        table.SelectRow(0).Decorate(bold);
        table.SelectColumn(0).DecorateCells(align_right | size(WIDTH, EQUAL, 7));
        table.SelectColumn(1).DecorateCells(size(WIDTH, EQUAL, 2));
        table.SelectColumn(2).DecorateCells(size(WIDTH, GREATER_THAN, 30));
        table.SelectColumn(3).DecorateCells(align_right | size(WIDTH, EQUAL, 7));
        // table.SelectColumn(4).DecorateCells(size(WIDTH, GREATER_THAN, 30));
        table.SelectColumn(4).DecorateCells(align_right | size(WIDTH, EQUAL, 12));



        // ── Highlight selected row (offset by 1 for header) ───────────────
        if (sel >= win_start && sel < win_end) {
            int table_row = (sel - win_start) + 1; // +1 for header
            table.SelectRow(table_row)
                 .DecorateCells(bgcolor(Color::Cyan) | color(Color::Black));
        }

        // ── Scroll indicator: position / total ────────────────────────────
        std::string scroll_info = std::to_string(sel + 1) + "/" + std::to_string(total);

        return vbox({
            table.Render(),
            // Shows current position without relying on frame's focus scroll
            text(" " + scroll_info + " ") | align_right | dim,
        }) | flex;
    }

    bool OnEvent(Event event) override {
        if (event == Event::ArrowUp)
            return parent_->MoveSelection(-1);
        if (event == Event::ArrowDown)
            return parent_->MoveSelection(1);
        if (event == Event::Return)
            return parent_->ActivateSelection();
        return false;
    }

    bool Focusable() const override { return true; }

private:
    ProcessTable* parent_;
};

// ── ProcessTable ──────────────────────────────────────────────────────

ProcessTable::ProcessTable(SelectionCallback on_select)
    : on_select_(std::move(on_select)) {
    table_component_ = Make<ProcessTableComponent>(this);
}

ftxui::Component ProcessTable::GetComponent() { return table_component_; }

void ProcessTable::UpdateData(std::vector<ProcessWatchInfo> data) {
    std::optional<int> prev_selected = SelectedPid();
    data_ = std::move(data);

    if (data_.empty()) {
        selected_index_ = -1;
        return;
    }

    if (prev_selected.has_value()) {
        for (size_t i = 0; i < data_.size(); ++i) {
            if (data_[i].pid == *prev_selected) {
                selected_index_ = static_cast<int>(i);
                return;
            }
        }
    }

    if (selected_index_ < 0)
        selected_index_ = 0;
    else if (selected_index_ >= static_cast<int>(data_.size()))
        selected_index_ = static_cast<int>(data_.size()) - 1;
}

bool ProcessTable::MoveSelection(int delta) {
    if (data_.empty()) return true;
    if (selected_index_ < 0) {
        selected_index_ = 0;
        return true;
    }
    int next = selected_index_ + delta;
    if (next >= 0 && next < static_cast<int>(data_.size()))
        selected_index_ = next;
    return true;
}

bool ProcessTable::ActivateSelection() {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(data_.size()))
        on_select_(data_[selected_index_]);
    return true;
}

std::optional<int> ProcessTable::SelectedPid() const {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(data_.size()))
        return data_[selected_index_].pid;
    return std::nullopt;
}