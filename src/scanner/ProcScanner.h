#pragma once
#include <vector>
#include <string>

struct ProcessInfo {
    int pid;
    std::string name;
};

class ProcScanner {
public:
    static std::vector<ProcessInfo> scan();
};