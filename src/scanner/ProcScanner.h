#pragma once
#include <string>
#include <vector>

struct ProcessInfo {
    int pid;
    std::string name;
};

class ProcScanner {
    public:
        // Every numeric directory in /proc together with its process name.
        static std::vector<ProcessInfo> scan();

        // Only the PIDs. Cheap: no file is opened per process.
        static std::vector<int> list_pids();

        // Reads /proc/<pid>/comm and sanitizes it. Returns "" if the process is gone.
        static std::string read_name(int pid);
};
