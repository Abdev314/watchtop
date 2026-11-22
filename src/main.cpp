#include "ui/Dashboard.h"
#include "scanner/WatchCollector.h"
#include "system/SystemLimits.h"
#include "ftxui/component/screen_interactive.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <pthread.h>
#include <string>
#include <thread>

// Set from CMakeLists.txt (project VERSION). The fallback only matters for odd build setups.
#ifndef WATCHTOP_VERSION
#define WATCHTOP_VERSION "unknown"
#endif

namespace {

constexpr auto kTick = std::chrono::milliseconds(100);
constexpr int  kDefaultInterval = 2;    // seconds between refreshes
constexpr int  kMaxInterval     = 60;

void print_help() {
    std::puts(
        "watchtop - see which processes use your inotify file watchers\n"
        "\n"
        "Usage: watchtop [OPTION]...\n"
        "\n"
        "Options:\n"
        "  --interval N   seconds between refreshes, 1 to 60 (default 2)\n"
        "  --once         print one plain-text report and exit (for scripts and tests)\n"
        "  --version      print the version and exit\n"
        "  -h, --help     show this help\n"
        "\n"
        "Without --once, watchtop opens the live dashboard (press q to quit).\n"
        "Every refresh reads /proc for all processes, which costs CPU: a longer\n"
        "interval means less CPU.");
}

// Reads the value of --interval. Returns 0 unless the text is a whole number from 1 to 60.
int parse_interval(const char* text) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 1 || value > kMaxInterval) return 0;
    return static_cast<int>(value);
}

// Prints one report and exits. Format (one value per line, then one process per line):
//   #watchtop <version>
//   max_user_watches=<n>
//   total_watches=<n>
//   processes=<n>
//   <pid> TAB <name> TAB <watches> TAB <ports separated by commas>
// Process names are already sanitized by the scanner, so no control characters are printed.
int run_once() {
    try {
        const InotifyLimits limits = SystemLimits::read();
        auto data = WatchCollector::collect();
        std::sort(data.begin(), data.end(),
                  [](const ProcessWatchInfo& a, const ProcessWatchInfo& b) {
                      return a.watch_count > b.watch_count;
                  });

        size_t total = 0;
        for (const auto& p : data) total += p.watch_count;

        std::printf("#watchtop %s\n", WATCHTOP_VERSION);
        std::printf("max_user_watches=%llu\n", static_cast<unsigned long long>(limits.max_user_watches));
        std::printf("total_watches=%zu\n", total);
        std::printf("processes=%zu\n", data.size());
        for (const auto& p : data) {
            std::string ports;
            for (const auto& port : p.listening_ports)
                ports += (ports.empty() ? "" : ",") + port;
            std::printf("%d\t%s\t%zu\t%s\n", p.pid, p.name.c_str(), p.watch_count, ports.c_str());
        }
        std::fflush(stdout);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "watchtop: scan failed: %s\n", e.what());
        return 1;
    }
}

// The live dashboard. `interval` is the number of seconds between automatic refreshes.
int run_dashboard(int interval) {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    std::atomic<bool> running{true};
    std::atomic<bool> refresh_requested{false};
    const int ticks_per_refresh = interval * 10;   // one tick = 100 ms

    // The worker thread calls screen.Post(). FTXUI resets its internal message channel when
    // the loop exits, so Post() must never run at the same time as the exit (a data race that
    // ThreadSanitizer reports). Quitting therefore stops and joins the worker FIRST.
    std::thread worker;
    auto stop_worker = [&] {
        running = false;
        if (worker.joinable()) worker.join();
    };

    // "q" (and Ctrl+C): stop the scanning thread, then leave the loop.
    // "r" only raises a flag that the worker thread reads.
    Dashboard dashboard([&] { stop_worker(); screen.Exit(); },
                        [&] { refresh_requested = true; },
                        interval);

    // First load happens here, before the worker thread exists, so nothing is shared yet.
    try {
        dashboard.ApplyData(SystemLimits::read(), WatchCollector::collect());
    } catch (const std::exception&) {
        // Start with an empty table; the worker will retry.
    }

    // Worker thread: reads /proc and sends a finished copy of the data to the UI thread.
    // It never touches the Dashboard directly, which removes the data race.
    worker = std::thread([&] {
        pthread_setname_np(pthread_self(), "wt-scan");   // shows up in top -H and in the test report
        int ticks = 0;
        while (running) {
            std::this_thread::sleep_for(kTick);

            const bool forced = refresh_requested.exchange(false);
            if (!forced && ++ticks < ticks_per_refresh) continue;
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

    stop_worker();   // already stopped if the user quit; this covers any other way out of the loop
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool once = false;
    int interval = kDefaultInterval;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::printf("watchtop %s\n", WATCHTOP_VERSION);
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        }
        if (arg == "--once") {
            once = true;
            continue;
        }
        if (arg == "--interval" || arg.starts_with("--interval=")) {
            const char* value = nullptr;
            if (arg == "--interval") {
                if (i + 1 < argc) value = argv[++i];
            } else {
                value = argv[i] + std::string("--interval=").size();
            }
            interval = value ? parse_interval(value) : 0;
            if (interval == 0) {
                std::fprintf(stderr, "watchtop: --interval needs a whole number of seconds from 1 to %d\n", kMaxInterval);
                return 2;
            }
            continue;
        }
        std::fprintf(stderr, "watchtop: unknown option '%s' (try --help)\n", argv[i]);
        return 2;
    }
    return once ? run_once() : run_dashboard(interval);
}