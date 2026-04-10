#pragma once
#include "ftxui/component/component.hpp"
#include <string>
#include <vector>

class CommandPanel {
public:
    CommandPanel();

    ftxui::Component GetComponent();

    void Focus();
    void Unfocus();
    bool Focused() const;
    void ExecuteCommand(const std::string& cmd);

    std::vector<std::string> output_lines_;
    std::vector<std::string> command_history_;
    int                      history_index_ = 0;
    bool                     focused_       = false;

private:
    ftxui::Component container_;
};