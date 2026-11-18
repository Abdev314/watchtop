#pragma once
#include <cstdint>
#include <string>

enum class KillSignal { Terminate, Kill };   // SIGTERM (polite) or SIGKILL (force)

// Safe helpers for stopping a process.
class ProcessControl {
    public:
        // Start time of a process in clock ticks since boot (field 22 of /proc/<pid>/stat).
        // Together with the PID it identifies one exact process. Returns 0 if unreadable.
        static uint64_t start_ticks(int pid);

        // Returns "" if WatchTop may signal this PID, otherwise the reason in plain words.
        static std::string check_allowed(int pid);

        // Sends the signal only if the PID still belongs to the process we showed
        // (same start time). Returns "" on success, otherwise an error message.
        static std::string send(int pid, uint64_t expected_start, KillSignal sig);
};