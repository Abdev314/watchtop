#include "ProcessControl.h"
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

// Syscall numbers are the same on every architecture; older headers may not define them.
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif

// Reads field 22 of /proc/<pid>/stat. The process name (field 2) sits in parentheses
// and may contain spaces or ')', so we count fields after the LAST ')'.
uint64_t ProcessControl::start_ticks(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    const std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const auto close = content.rfind(')');
    if (close == std::string::npos) return 0;

    // After ')' the first field is number 3 (state), so field 22 is the 20th token.
    std::istringstream rest(content.substr(close + 1));
    std::string tok;
    for (int i = 1; i <= 20; ++i)
        if (!(rest >> tok)) return 0;

    uint64_t value = 0;
    const auto res = std::from_chars(tok.data(), tok.data() + tok.size(), value);
    return res.ec == std::errc{} ? value : 0;
}

// Rules that never change: no PID 1, never ourselves, only our own processes unless root.
std::string ProcessControl::check_allowed(int pid) {
    if (pid <= 1)       return "Refusing to signal PID 1 or an invalid PID.";
    if (pid == getpid()) return "That is WatchTop itself.";

    struct stat st{};
    if (::stat(("/proc/" + std::to_string(pid)).c_str(), &st) != 0)
        return "Process no longer exists.";

    if (geteuid() != 0 && st.st_uid != geteuid())
        return "Owned by another user. Run WatchTop with sudo to stop it.";

    return "";
}

// Stops a process without PID-reuse mistakes:
//   1. the start time must still match what we showed the user,
//   2. we pin the process with a pidfd, check the start time AGAIN,
//   3. then signal through the pidfd, which can only reach that exact process.
// Old kernels without pidfd fall back to kill(), after the start-time check.
std::string ProcessControl::send(int pid, uint64_t expected_start, KillSignal sig) {
    if (auto why = check_allowed(pid); !why.empty()) return why;

    const std::string reused = "The PID changed or the process already exited. Nothing was sent.";
    if (expected_start == 0 || start_ticks(pid) != expected_start) return reused;

    const int signo = (sig == KillSignal::Kill) ? SIGKILL : SIGTERM;

    const long pidfd = ::syscall(SYS_pidfd_open, pid, 0);
    if (pidfd >= 0) {
        long rc = -1;
        int  err = 0;
        const bool same = (start_ticks(pid) == expected_start);
        if (same) {
            rc  = ::syscall(SYS_pidfd_send_signal, pidfd, signo, nullptr, 0);
            err = errno;
        }
        ::close(static_cast<int>(pidfd));

        if (!same)    return reused;
        if (rc == 0)  return "";
        return std::strerror(err);
    }

    if (errno == ESRCH) return "Process already exited.";
    if (errno != ENOSYS && errno != EPERM) return std::strerror(errno);

    // pidfd not available (old kernel or restricted container): plain kill().
    if (::kill(pid, signo) == 0) return "";
    return std::strerror(errno);
}