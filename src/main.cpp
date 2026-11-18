#include "ui/Dashboard.h"
#include "scanner/WatchCollector.h"
#include "system/SystemLimits.h"
#include "ftxui/component/screen_interactive.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <thread>

namespace {
constexpr auto kTick = std::chrono::milliseconds(100);
constexpr int  kTicksPerRefresh = 10;   // 10 ticks x 100 ms = refresh every 1 second
}

int main() {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    std::atomic<bool> running{true};
    std::atomic<bool> refresh_requested{false};

    // "q" closes the UI loop; "r" only raises a flag that the worker thread reads.
    Dashboard dashboard(screen.ExitLoopClosure(),
                        [&] { refresh_requested = true; });

    // First load happens here, before the worker thread exists, so nothing is shared yet.
    try {
        dashboard.ApplyData(SystemLimits::read(), WatchCollector::collect());
    } catch (const std::exception&) {
        // Start with an empty table; the worker will retry.
    }

    // Worker thread: reads /proc and sends a finished copy of the data to the UI thread.
    // It never touches the Dashboard directly, which removes the data race.
    std::thread worker([&] {
        int ticks = 0;
        while (running) {
            std::this_thread::sleep_for(kTick);

            const bool forced = refresh_requested.exchange(false);
            if (!forced && ++ticks < kTicksPerRefresh) continue;
            ticks = 0;

            try {
                auto limits = SystemLimits::read();
                auto data   = WatchCollector::collect();
                screen.Post([&dashboard, limits, data = std::move(data)]() mutable {
                    dashboard.ApplyData(limits, std::move(data));   // runs on the UI thread
                });
                screen.PostEvent(Event::Custom);                    // request a redraw
            } catch (const std::exception&) {
                // Skip this refresh (for example if /proc changed while scanning).
            }
        }
    });

    screen.Loop(dashboard.GetComponent());

    running = false;
    worker.join();
    return 0;
}