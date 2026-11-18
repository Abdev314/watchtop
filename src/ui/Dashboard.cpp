#include "Dashboard.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <unistd.h>

using namespace ftxui;

namespace {

constexpr auto kFlashTime = std::chrono::seconds(2);   // how long "Refreshed!" stays visible

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// True if the (already lower-case) filter appears in the PID, name or any port.
bool matches(const ProcessWatchInfo& p, const std::string& filter) {
    if (filter.empty()) return true;
    if (std::to_string(p.pid).find(filter) != std::string::npos) return true;
    if (to_lower(p.name).find(filter) != std::string::npos) return true;
    for (const auto& port : p.listening_ports)
        if (port.find(filter) != std::string::npos) return true;
    return false;
}

// Thread-safe local time, e.g. "14:05:09".
std::string now_string() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

// Number to text with a fixed count of decimals.
std::string num(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} 

Dashboard::Dashboard(std::function<void()> on_quit, std::function<void()> on_refresh)
    : on_quit_(std::move(on_quit)),
      on_refresh_(std::move(on_refresh)),
      is_root_(geteuid() == 0) {

    watch_details_ = std::make_unique<WatchDetails>();

    // Search box: the table is filtered on every keystroke.
    InputOption search_opt;
    search_opt.on_change = [this] { ApplyFilter(); };
    search_input_ = Input(&search_filter_, "Search by PID, name or port...", search_opt);

    // Pressing Enter on a row shows its watches in the right panel.
    process_table_ = std::make_unique<ProcessTable>(
        [this](const ProcessWatchInfo& info) {
            selected_pid_ = info.pid;
            watch_details_->SetProcess(&info);
        });

    // Top-left box: limits, usage (number + gauge) and refresh status.
    system_panel_ = Renderer([this] {
        const bool   limit_known = limits_.max_user_watches > 0;
        const double usage       = limits_.usage_percent();
        const Color  usage_color = usage > 80 ? Color::Red : (usage > 60 ? Color::Yellow : Color::Green);

        // Small percentages need decimals, otherwise 0.09% is shown as "0%".
        const std::string usage_text =
            !limit_known ? "unknown (limit unreadable)"
                         : num(usage, usage < 10 ? 2 : 1) + "%";

        Elements rows;
        rows.push_back(text("System Watch Status") | bold);
        rows.push_back(separator());
        rows.push_back(hbox({text("Max User Watches:  "), text(std::to_string(limits_.max_user_watches)) | dim}));
        rows.push_back(hbox({text("Max User Instances:"), text(std::to_string(limits_.max_user_instances)) | dim}));
        rows.push_back(hbox({text("Max Queued Events: "), text(std::to_string(limits_.max_queued_events)) | dim}));
        rows.push_back(separator());
        rows.push_back(hbox({text("Active Watches:    "),
                             text(std::to_string(limits_.current_watches)) | color(usage_color)}));
        rows.push_back(hbox({text("Usage:             "), text(usage_text) | color(usage_color) | bold}));
        rows.push_back(hbox({text("["),
                             gauge(static_cast<float>(std::min(usage, 100.0) / 100.0)) | color(usage_color) | flex,
                             text("]")}));
        if (usage > 80)
            rows.push_back(text("WARNING: Watch usage above 80%") | color(Color::Red) | bold);

        rows.push_back(separator());
        rows.push_back(text(is_root_ ? "Mode: root (all users counted)"
                                     : "Mode: user (own processes only)") | dim);
        rows.push_back(text("Last update: " + last_refresh_ + "  (#" + std::to_string(refresh_count_) + ")") | dim);
        rows.push_back(RefreshStatus());
        return vbox(std::move(rows)) | border;
    });

    // Only the search box and the table take keyboard focus.
    dashboard_container_ = Container::Vertical({search_input_, process_table_->GetComponent()});
    FocusTable();

    main_container_ = Renderer(dashboard_container_, [this] {
        return vbox({
            text(" WATCHTOP DASHBOARD ") | bold | center | border,
            hbox({
                system_panel_->Render() | size(WIDTH, EQUAL, 40),
                vbox({
                    history_tracker_.render(limits_.max_user_watches),
                    separator(),
                    TopConsumers(),
                }) | border | flex,
            }) | flex,
            hbox({text(" 🔍 "), search_input_->Render() | flex}) | border,
            hbox({
                process_table_->GetComponent()->Render() | yframe | flex | size(WIDTH, EQUAL, 60),
                text("") | size(WIDTH, EQUAL, 2) | size(HEIGHT, EQUAL, 30),
                watch_details_->GetComponent()->Render() | yframe | flex | size(WIDTH, EQUAL, 60),
            }) | size(HEIGHT, EQUAL, 30),
            text("↑↓: Navigate  ↵: Inspect  Tab: Switch  /: Search  Esc: Back  r: Refresh  q: Quit") | dim | center,
        }) | size(HEIGHT, EQUAL, Terminal::Size().dimy);
    });

    // Keyboard shortcuts. While the search box is active, letters go to the box,
    // so "q", "r" and "/" can be typed normally.
    main_container_ |= CatchEvent([this](Event event) {
        if (event == Event::Tab) {
            SearchFocused() ? FocusTable() : FocusSearch();
            return true;
        }
        if (SearchFocused()) {
            if (event == Event::Escape || event == Event::Return) {
                FocusTable();
                return true;
            }
            return false;
        }
        if (event == Event::Character('q')) { on_quit_(); return true; }
        if (event == Event::Character('r')) {
            manual_refresh_pending_ = true;   // shows "Refreshing..." until new data arrives
            on_refresh_();
            return true;
        }
        if (event == Event::Character('/')) { FocusSearch(); return true; }
        return false;
    });
}

ftxui::Component Dashboard::GetComponent() { return main_container_; }

bool Dashboard::SearchFocused() const {
    return dashboard_container_->ActiveChild() == search_input_;
}

void Dashboard::FocusSearch() {
    dashboard_container_->SetActiveChild(search_input_);
    search_input_->TakeFocus();
}

void Dashboard::FocusTable() {
    dashboard_container_->SetActiveChild(process_table_->GetComponent());
    process_table_->GetComponent()->TakeFocus();
}

// One fixed line (so the layout never jumps) that tells the user what the refresh is doing:
//   yellow = manual refresh requested, green = manual refresh done, dim = normal auto-refresh.
Element Dashboard::RefreshStatus() const {
    if (manual_refresh_pending_)
        return text(" ⟳ Refreshing... ") | bold | color(Color::Black) | bgcolor(Color::Yellow);
    if (std::chrono::steady_clock::now() < manual_flash_until_)
        return text(" ✔ Refreshed! ") | bold | color(Color::Black) | bgcolor(Color::Green);
    return text("Auto-refresh: every 1s") | dim;
}

// Top 3 processes by watch count (with their share of the total) and a leak summary.
Element Dashboard::TopConsumers() const {
    Elements rows;
    rows.push_back(text("Top consumers") | bold);

    if (top_.empty())
        rows.push_back(text("  none") | dim);

    for (size_t i = 0; i < top_.size(); ++i) {
        const auto& t = top_[i];
        const double share = limits_.current_watches > 0
            ? 100.0 * static_cast<double>(t.count) / static_cast<double>(limits_.current_watches)
            : 0.0;
        rows.push_back(hbox({
            text(" " + std::to_string(i + 1) + ". "),
            text(t.name + " (" + std::to_string(t.pid) + ")") | size(WIDTH, EQUAL, 30),
            text(std::to_string(t.count)) | align_right | size(WIDTH, EQUAL, 8),
            text(num(share, 1) + "%") | dim | align_right | size(WIDTH, EQUAL, 8),
        }));
    }

    if (leaking_.empty())
        rows.push_back(text("Leak check: no leaks detected") | color(Color::Green));
    else
        rows.push_back(text("Possible leak: " + leaking_) | color(Color::Yellow) | bold);

    return vbox(std::move(rows));
}

// Stores new data. Totals, history and leak detection use ALL processes,
// so typing in the search box never changes them.
void Dashboard::ApplyData(const InotifyLimits& limits, std::vector<ProcessWatchInfo> data) {
    limits_ = limits;

    const auto leaks = leak_detector_.update(data);
    uint64_t total = 0;
    for (auto& p : data) {
        total += p.watch_count;
        if (auto it = leaks.find(p.pid); it != leaks.end()) {
            p.is_leaking = it->second.is_leaking;
            p.leak_rate  = it->second.leak_rate;
        }
    }
    limits_.current_watches = total;
    history_tracker_.add_sample(total);

    all_processes_ = std::move(data);

    // Top 3 consumers.
    std::vector<const ProcessWatchInfo*> ranked;
    for (const auto& p : all_processes_) ranked.push_back(&p);
    const size_t n = std::min<size_t>(3, ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(n), ranked.end(),
                      [](const ProcessWatchInfo* a, const ProcessWatchInfo* b) {
                          return a->watch_count > b->watch_count;
                      });
    top_.clear();
    for (size_t i = 0; i < n; ++i)
        top_.push_back({ranked[i]->pid, ranked[i]->name, ranked[i]->watch_count});

    // Leaking processes as "name (pid)": first 3, then "+N more".
    leaking_.clear();
    size_t leak_count = 0;
    for (const auto& p : all_processes_) {
        if (!p.is_leaking) continue;
        if (leak_count < 3)
            leaking_ += (leak_count ? ", " : "") + p.name + " (" + std::to_string(p.pid) + ")";
        ++leak_count;
    }
    if (leak_count > 3) leaking_ += " +" + std::to_string(leak_count - 3) + " more";

    // Refresh feedback: remember when data arrived; confirm a manual refresh for 2 seconds.
    last_refresh_ = now_string();
    ++refresh_count_;
    if (manual_refresh_pending_) {
        manual_refresh_pending_ = false;
        manual_flash_until_ = std::chrono::steady_clock::now() + kFlashTime;
    }

    ApplyFilter();
}

// Rebuilds the visible list from the search text and refreshes the right panel.
// Must run after all_processes_ changes, because the other panels point into it.
void Dashboard::ApplyFilter() {
    const std::string filter = to_lower(search_filter_);

    visible_.clear();
    for (const auto& p : all_processes_)
        if (matches(p, filter)) visible_.push_back(&p);

    process_table_->UpdateData(visible_);

    watch_details_->Clear();
    if (!selected_pid_) return;

    auto it = std::find_if(visible_.begin(), visible_.end(),
                           [&](const ProcessWatchInfo* p) { return p->pid == *selected_pid_; });
    if (it == visible_.end())
        selected_pid_.reset();                 // process exited or is filtered out
    else
        watch_details_->SetProcess(*it);
}