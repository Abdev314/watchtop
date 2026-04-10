#include "ui/Dashboard.h"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

using namespace ftxui;

static std::atomic<bool> running{true};

void signal_handler(int) { running = false; }

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto screen = ScreenInteractive::Fullscreen();
    Dashboard dashboard;
    auto component = dashboard.GetComponent();

    component |= CatchEvent([&](Event event) {
        if (event == Event::Character('q')) {
            running = false;
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('r')) {
            dashboard.RefreshData();
            return true;
        }
        return false;
    });

    std::thread refresh_thread([&]() {
        while (running) {
            dashboard.RefreshData();
            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    screen.Loop(component);
    running = false;
    if (refresh_thread.joinable())
        refresh_thread.join();

    return 0;
}