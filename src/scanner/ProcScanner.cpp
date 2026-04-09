#include "ProcScanner.h"
#include <filesystem>
#include <fstream>
#include <cctype>
#include <algorithm>

namespace fs = std::filesystem;

std::vector<ProcessInfo> ProcScanner::scan() {
    std::vector<ProcessInfo> processes;
    const std::string proc_path = "/proc";

    for (const auto& entry : fs::directory_iterator(proc_path)) {
        if (!entry.is_directory()) continue;

        std::string dirname = entry.path().filename().string();
        // Check if directory name is numeric (PID)
        if (dirname.empty() || !std::all_of(dirname.begin(), dirname.end(), ::isdigit))
            continue;

        int pid = std::stoi(dirname);
        std::string comm_path = entry.path().string() + "/comm";
        std::ifstream comm_file(comm_path);
        std::string name;
        if (std::getline(comm_file, name)) {
            // Remove trailing newline if any
            if (!name.empty() && name.back() == '\n')
                name.pop_back();
            processes.push_back({pid, std::move(name)});
        }
    }
    return processes;
}