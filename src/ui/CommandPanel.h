#pragma once
#include <vector>
#include <string>
#include "ftxui/component/component.hpp"

class CommandPanel {
public:
    CommandPanel();
    ftxui::Component GetComponent();
    void Focus();
    bool Focused() const;

private:
    friend class CommandPanelImpl;
    std::vector<std::string> output_lines_;
    std::vector<std::string> command_history_;
    int history_index_ = -1;
    ftxui::Component container_;
    bool focused_ = false;

    void ExecuteCommand(const std::string& cmd);
};