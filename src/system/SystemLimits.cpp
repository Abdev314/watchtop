#include "SystemLimits.h"
#include <fstream>
#include <string>

static uint64_t read_uint64_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return 0;
    uint64_t value = 0;
    file >> value;
    return value;
}

InotifyLimits SystemLimits::read() {
    InotifyLimits limits;
    limits.max_user_watches   = read_uint64_from_file("/proc/sys/fs/inotify/max_user_watches");
    limits.max_user_instances = read_uint64_from_file("/proc/sys/fs/inotify/max_user_instances");
    limits.max_queued_events  = read_uint64_from_file("/proc/sys/fs/inotify/max_queued_events");
    return limits;
}

