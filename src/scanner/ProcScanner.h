#pragma once
#include <vector>
#include <string>

struct ProcessInfo {
    int pid;
    std::string name;
};

class ProcScanner {
public:
    // Returns list of running processes (PID and command name)
    static std::vector<ProcessInfo> scan();
};