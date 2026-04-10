#include "ProcessTable.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"

using namespace ftxui;

class ProcessTableComponent : public ComponentBase {
public:
    explicit ProcessTableComponent(ProcessTable* parent) : parent_(parent) {}

    Element Render() override {
        std::vector<std::vector<std::string>> table_content;
        table_content.push_back({"PID", "Process", "Watches"});
        for (const auto& p : parent_->data_) {
            table_content.push_back({
                std::to_string(p.pid),
                p.name,
                std::to_string(p.watch_count)
            });
        }

        auto table = Table(table_content);
        table.SelectAll().Border(LIGHT);
        table.SelectRow(0).Decorate(bold);
        table.SelectColumn(0).DecorateCells(align_right);
        table.SelectColumn(2).DecorateCells(align_right);

        if (parent_->selected_index_ >= 0 && parent_->selected_index_ < static_cast<int>(parent_->data_.size())) {
            table.SelectRow(parent_->selected_index_ + 1)
                 .DecorateCells(bgcolor(Color::Cyan) | color(Color::Black));
        }

        return table.Render() | vscroll_indicator | frame | flex;
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