#include "CommandPanel.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include <cstdio>
#include <array>
#include <algorithm>

using namespace ftxui;

class CommandPanelImpl : public ComponentBase {
public:
    explicit CommandPanelImpl(CommandPanel* parent) : parent_(parent) {
        InputOption opt;
        opt.multiline = false;
        opt.on_enter = [this] {
            if (!input_.empty()) {
                parent_->command_history_.push_back(input_);
                parent_->history_index_ = static_cast<int>(parent_->command_history_.size());
                parent_->ExecuteCommand(input_);
                input_.clear();
            }
            return true;
        };
        input_component_ = Input(&input_, "", opt);
        Add(input_component_);
    }

    Element Render() override {
        Elements output_elements;
        int start = std::max(0, static_cast<int>(parent_->output_lines_.size()) - 20);
        for (size_t i = start; i < parent_->output_lines_.size(); ++i) {
            const auto& line = parent_->output_lines_[i];
            if (!line.empty() && line[0] == '$')
                output_elements.push_back(text(line) | color(Color::Cyan));
            else if (line.rfind("[exit code:", 0) == 0)
                output_elements.push_back(text(line) | color(Color::Red));
            else
                output_elements.push_back(text(line));
        }

        if (parent_->output_lines_.empty())
            output_elements.push_back(text("Type a command below and press Enter") | dim);

        auto input_area = hbox({
            text("> ") | color(Color::Cyan) | bold,
            input_component_->Render() | flex,
        });

        return vbox({
            vbox(std::move(output_elements)) | flex,
            separator(),
            input_area,
        }) | (parent_->focused_ ? borderDouble : border);
    }

    bool OnEvent(Event event) override {
        if (event == Event::ArrowUp) {
            if (!parent_->command_history_.empty() && parent_->history_index_ > 0) {
                parent_->history_index_--;
                input_ = parent_->command_history_[parent_->history_index_];
            }
            return true;
        }
        if (event == Event::ArrowDown) {
            if (parent_->history_index_ <
                static_cast<int>(parent_->command_history_.size()) - 1) {
                parent_->history_index_++;
                input_ = parent_->command_history_[parent_->history_index_];
            } else {
                parent_->history_index_ =
                    static_cast<int>(parent_->command_history_.size());
                input_.clear();
            }
            return true;
        }

        return input_component_->OnEvent(event);
    }

    bool Focusable() const override { return true; }

private:
    CommandPanel* parent_;
    Component     input_component_;
    std::string   input_;
};

CommandPanel::CommandPanel() {
    container_ = Make<CommandPanelImpl>(this);
}

ftxui::Component CommandPanel::GetComponent() { return container_; }

void CommandPanel::Focus()   { focused_ = true;  if (container_) container_->TakeFocus(); }
void CommandPanel::Unfocus() { focused_ = false; }
bool CommandPanel::Focused() const { return focused_; }

void CommandPanel::ExecuteCommand(const std::string& cmd) {
    output_lines_.push_back("$ " + cmd);

    std::string escaped;
    for (char c : cmd) {
        if (c == '\'') escaped += "'\\''";
        else           escaped += c;
    }

    std::array<char, 256> buffer;
    FILE* pipe = popen(("/bin/sh -c '" + escaped + "' 2>&1").c_str(), "r");
    if (!pipe) {
        output_lines_.push_back("  Error: failed to open pipe");
        return;
    }

    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        result += buffer.data();
    int status = pclose(pipe);

    std::string line;
    for (char c : result) {
        if (c == '\n') {
            output_lines_.push_back("  " + line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty())
        output_lines_.push_back("  " + line);

    if (status != 0)
        output_lines_.push_back("[exit code: " + std::to_string(status) + "]");
}