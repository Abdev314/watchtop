# watchtop test report

- **Result:** 29 passed, 0 failed, 0 errors, 2 skipped
- **Date:** 2026-11-22T13:41:30+08:00   **Commit:** `2d238e1 (+uncommitted changes)`   **Binary:** `/home/iron/dev/Projects/watchtop/build/watchtop`
- **System:** Debian GNU/Linux 13 (trixie), kernel 6.12.113+deb13-amd64, g++ (Debian 14.2.0-19) 14.2.0
- **Run as:** normal user   **max_user_watches:** 65536

| # | Test | Result | Time | Details |
|---|------|--------|------|---------|
| 1 | build: binary exists and is executable | **PASS** | 0.0s |  |
| 2 | build: Release build type | **PASS** | 0.0s |  |
| 3 | build: hardening (PIE, BIND_NOW, stack protector) | **PASS** | 0.0s |  |
| 4 | cli: --version matches CMakeLists.txt | **PASS** | 0.0s | watchtop 0.1.0 |
| 5 | cli: --help | **PASS** | 0.0s |  |
| 6 | cli: unknown option is rejected with exit code 2 | **PASS** | 0.0s |  |
| 7 | cli: --interval is validated | **PASS** | 0.2s |  |
| 8 | scan: --once output format and totals | **PASS** | 0.1s | 66 processes, 18713 watches |
| 9 | count: 1 watches match /proc | **PASS** | 0.1s | watchtop=1 /proc=1 |
| 10 | count: 15 watches match /proc | **PASS** | 0.1s | watchtop=15 /proc=15 |
| 11 | count: 16 watches match /proc | **PASS** | 0.1s | watchtop=16 /proc=16 |
| 12 | count: 17 watches match /proc | **PASS** | 0.1s | watchtop=17 /proc=17 |
| 13 | count: 300 watches match /proc | **PASS** | 0.1s | watchtop=300 /proc=300 |
| 14 | count: 2000 watches match /proc | **PASS** | 0.1s | watchtop=2000 /proc=2000 |
| 15 | count: 50 watches in 3 inotify instances match /proc | **PASS** | 0.1s | watchtop=150 /proc=150 |
| 16 | ports: listening TCP port is reported | **PASS** | 0.1s | port 46489 |
| 17 | security: terminal escape codes in a process name are neutralized | **PASS** | 0.1s |  |
| 18 | security: C1 control characters (UTF-8 U+0080..U+009F) in a name are neutralized | **PASS** | 0.1s |  |
| 19 | security: other users' processes are not listed (normal user) | **SKIP** | 0.0s | passwordless sudo not available |
| 20 | stability: --once survives process churn | **PASS** | 1.3s | 6 runs |
| 21 | stability: dashboard survives process churn | **PASS** | 9.9s |  |
| 22 | tui: starts and quits with q | **PASS** | 2.4s |  |
| 23 | tui: terminal is restored on quit | **PASS** | 1.4s |  |
| 24 | tui: letters typed in the search box are not shortcuts | **PASS** | 2.8s |  |
| 25 | tui: r shows 'Refreshed!' | **PASS** | 2.1s |  |
| 26 | kill: Esc cancels, nothing is sent | **PASS** | 4.1s |  |
| 27 | kill: Enter sends SIGTERM | **PASS** | 2.5s |  |
| 28 | kill: f sends SIGKILL when SIGTERM is ignored | **PASS** | 5.1s |  |
| 29 | perf: one scan costs little CPU | **PASS** | 0.6s | median 63.2 ms per scan, 7 runs (limit 100.0 ms); one scan per second is about 6.3% CPU |
| 30 | perf: idle CPU and memory | **PASS** | 23.4s | 20s: cpu avg 5.10% max 5.10% (limit 6.0%), rss 7.2 -> 9.3 MB; threads: wt-scan 4.2%, watchtop 0.3%, watchtop 0.3%, watchtop 0.2% |
| 31 | sanitizer: no reports were written | **SKIP** | 0.0s | binary is not built with a sanitizer |
