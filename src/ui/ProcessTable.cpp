#include "ProcessTable.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"

using namespace ftxui;

ProcessTable::ProcessTable(SelectionCallback on_select)
    : on_select_(std::move(on_select)) {

    // Create a component that is both renderable and interactive
    class Impl : public ComponentBase {
    public:
        Impl(ProcessTable* parent) : parent_(parent) {}

        Element Render() override {
            std::vector<std::vector<std::string>> table_content;
            table_content.push_back({"PID", "Process", "Watches"});
            for (size_t i = 0; i < parent_->data_.size(); ++i) {
                const auto& p = parent_->data_[i];
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

            // Highlight selected row
            if (!parent_->data_.empty() && parent_->selected_index_ >= 0 &&
                parent_->selected_index_ < static_cast<int>(parent_->data_.size())) {
                table.SelectRow(parent_->selected_index_ + 1).DecorateCells(bgcolor(Color::Cyan) | color(Color::Black));
            }

            return table.Render() | vscroll_indicator | frame | flex;
        }

        bool OnEvent(Event event) override {
            if (event == Event::ArrowUp && parent_->selected_index_ > 0) {
                parent_->selected_index_--;
                return true;
            }
            if (event == Event::ArrowDown && parent_->selected_index_ < static_cast<int>(parent_->data_.size()) - 1) {
                parent_->selected_index_++;
                return true;
            }
            if (event == Event::Return) {
                if (parent_->selected_index_ >= 0 && parent_->selected_index_ < static_cast<int>(parent_->data_.size()))
                    parent_->on_select_(parent_->data_[parent_->selected_index_]);
                return true;
            }
            return false;
        }

        bool Focusable() const override { return true; }

    private:
        ProcessTable* parent_;
    };

    table_component_ = Make<Impl>(this);
}

ftxui::Component ProcessTable::GetComponent() { return table_component_; }

void ProcessTable::UpdateData(std::vector<ProcessWatchInfo> data) {
    data_ = std::move(data);
    if (selected_index_ >= static_cast<int>(data_.size()))
        selected_index_ = data_.empty() ? 0 : data_.size() - 1;
}