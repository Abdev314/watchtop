#pragma once
#include <cstdint>

struct InotifyLimits {
    uint64_t max_user_watches = 0;
    uint64_t max_user_instances = 0;
    uint64_t max_queued_events = 0;
    uint64_t current_watches = 0;

    [[nodiscard]] double usage_percent() const {
        if (max_user_watches == 0) return 0.0;
        return (static_cast<double>(current_watches) / max_user_watches) * 100.0;
    }
};

class SystemLimits {
public:
    static InotifyLimits read();
};