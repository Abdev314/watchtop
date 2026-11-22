#!/usr/bin/env python3
"""
watchtop automated test suite (Python 3, standard library only).

    python3 tests/run_tests.py                         run everything
    python3 tests/run_tests.py --list                  show all tests
    python3 tests/run_tests.py --only kill,count       run tests whose name contains a word
    python3 tests/run_tests.py --long 1800             30-minute CPU / memory run
    python3 tests/run_tests.py --bin build-asan/watchtop --label asan

Every test ends as PASS, FAIL, ERROR (the test itself crashed) or SKIP (cannot run here).
Results are written to ONE place:  tests/results/report[-label].md  and  .json
and one line per test is appended to tests/results/history.csv.
Exit code: 0 = no failures (skips are fine), 1 = at least one FAIL or ERROR.
"""
import argparse
import csv
import datetime
import fcntl
import json
import os
import platform
import pty
import re
import select
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time
import traceback
from pathlib import Path
from typing import Callable, Optional

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "tests" / "results"
SANITIZER_LOGS = RESULTS / "sanitizer_logs"

BIN: Path = Path()                                # watchtop binary under test (set in main)
CFG: argparse.Namespace = argparse.Namespace()    # parsed command-line options (set in main)


class Skip(Exception):
    """Raised by a test that cannot run on this machine."""


TestFn = Callable[[], Optional[str]]   # a test returns a short detail text, or nothing
TESTS: list[tuple[str, TestFn]] = []   # (name, function), in registration order


def test(name: str) -> Callable[[TestFn], TestFn]:
    def deco(fn: TestFn) -> TestFn:
        TESTS.append((name, fn))
        return fn
    return deco


# --------------------------------------------------------------------------- helpers

def tool_env():
    """Environment for every watchtop run. Sanitizer reports go to one folder."""
    env = dict(os.environ, TERM="xterm-256color")
    env["ASAN_OPTIONS"] = f"log_path={SANITIZER_LOGS}/asan"
    env["UBSAN_OPTIONS"] = f"log_path={SANITIZER_LOGS}/ubsan:print_stacktrace=1"
    env["TSAN_OPTIONS"] = f"log_path={SANITIZER_LOGS}/tsan"
    return env


def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[()][0-9A-Za-z]|\x1b[=>78]")


def plain(data):
    """Terminal output without colors and cursor codes, as readable text."""
    return ANSI.sub(b"", data).decode("utf-8", "replace")


def is_instrumented():
    """True if the binary was built with a sanitizer (AddressSanitizer, UBSan or ThreadSanitizer)."""
    if not shutil.which("readelf"):
        return False
    symbols = sh(["readelf", "-sW", str(BIN)])
    return any(name in symbols for name in ("__asan_init", "__tsan_init", "__ubsan_handle"))


def run_once(timeout=30):
    return subprocess.run([str(BIN), "--once"], capture_output=True, timeout=timeout, env=tool_env())


def parse_once(raw):
    """Parses the output of `watchtop --once` into (info dict, list of process rows)."""
    info, rows = {}, []
    for line in raw.decode("utf-8", "replace").split("\n"):
        if not line:
            continue
        if line.startswith("#"):
            info["banner"] = line
        elif "\t" in line:
            parts = line.split("\t")
            if len(parts) == 4:
                rows.append({"pid": int(parts[0]), "name": parts[1], "watches": int(parts[2]),
                             "ports": [p for p in parts[3].split(",") if p]})
        elif "=" in line:
            key, value = line.split("=", 1)
            info[key] = value
    return info, rows


def find_row(pid, tries=5):
    """Returns (row or None, raw output, info) for one PID, retrying while it appears."""
    raw, info = b"", {}
    for _ in range(tries):
        p = run_once()
        assert p.returncode == 0, f"--once exited with {p.returncode}: {p.stderr[:200]!r}"
        raw = p.stdout
        info, rows = parse_once(raw)
        for row in rows:
            if row["pid"] == pid:
                return row, raw, info
        time.sleep(0.4)
    return None, raw, info


def real_count(pid):
    """Counts inotify watches straight from /proc, independently of watchtop."""
    total = 0
    for fd in os.listdir(f"/proc/{pid}/fd"):
        try:
            if os.readlink(f"/proc/{pid}/fd/{fd}") != "anon_inode:inotify":
                continue
            with open(f"/proc/{pid}/fdinfo/{fd}") as f:
                total += sum(1 for line in f if line.startswith("inotify"))
        except OSError:
            pass
    return total


# ------------------------------------------------------- helper processes (test subjects)

# A small program that owns inotify watches, optionally listens on a port, ignores SIGTERM,
# or has a hostile process name. It prints:  <pid> <watches created> <port> <temp dir>
HELPER_SRC = r'''
import ctypes, os, signal, socket, sys, tempfile, time
n, want_port, ignore_term, evil, inst = (int(x) for x in sys.argv[1:6])
libc = ctypes.CDLL("libc.so.6", use_errno=True)
names = {0: b"wt_test_helper", 1: b"A\x1b[2JB\x1b]0;HI\x07", 2: b"A\xc2\x9b2JB"}
libc.prctl(15, names[evil], 0, 0, 0)
if ignore_term:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
base = tempfile.mkdtemp(prefix="wt_")
ok = 0
for k in range(inst):
    fd = libc.inotify_init()
    for i in range(n):
        d = os.path.join(base, "d%d_%d" % (k, i))
        os.mkdir(d)
        if libc.inotify_add_watch(fd, d.encode(), 0x2) >= 0:
            ok += 1
port = 0
if want_port:
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    s.listen()
    port = s.getsockname()[1]
print(os.getpid(), ok, port, base, flush=True)
time.sleep(900)
'''

HELPERS: list["Helper"] = []


class Helper:
    def __init__(self, proc, pid, ok, port, base, as_root):
        self.proc, self.pid, self.ok, self.port, self.base, self.as_root = proc, pid, ok, port, base, as_root


def spawn_helper(n=10, port=False, ign=False, evil=0, inst=1, as_root=False):
    cmd = ["sudo", "-n"] if as_root else []
    cmd += [sys.executable, "-c", HELPER_SRC, str(n), str(int(port)), str(int(ign)), str(evil), str(inst)]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)
    assert proc.stdout is not None
    ready, _, _ = select.select([proc.stdout], [], [], 15)
    assert ready, "helper process did not start in time"
    line = proc.stdout.readline()
    assert line, "helper process exited early"
    pid, ok, prt, base = line.split()
    helper = Helper(proc, int(pid), int(ok), int(prt), base, as_root)
    HELPERS.append(helper)
    return helper


def cleanup_helpers():
    while HELPERS:
        h = HELPERS.pop()
        try:
            if h.as_root:
                subprocess.run(["sudo", "-n", "kill", "-9", str(h.pid)], capture_output=True)
                subprocess.run(["sudo", "-n", "rm", "-rf", h.base], capture_output=True)
            else:
                h.proc.kill()
                shutil.rmtree(h.base, ignore_errors=True)
            h.proc.wait(timeout=5)
        except Exception:
            pass


# ------------------------------------------------------------ the dashboard in a pseudo-terminal

class Tui:
    """Runs the dashboard inside a pseudo-terminal, sends keys and reads the screen text."""

    def __init__(self, rows=60, cols=160):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        env = tool_env()
        env["LINES"], env["COLUMNS"] = str(rows), str(cols)
        self.proc = subprocess.Popen([str(BIN)], stdin=slave, stdout=slave, stderr=slave,
                                     env=env, start_new_session=True, close_fds=True)
        os.close(slave)
        self.buf = bytearray()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        os.close(self.master)

    def pump(self, seconds, keep=True):
        """Reads output for a while. It must always be read, or the program blocks on a full pipe."""
        end = time.time() + seconds
        while True:
            left = end - time.time()
            if left <= 0:
                return
            ready, _, _ = select.select([self.master], [], [], min(left, 0.1))
            if ready:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    time.sleep(0.05)
                    return
                if not data:
                    return
                if keep:
                    self.buf += data

    def mark(self):
        return len(self.buf)

    def text(self, since=0):
        return plain(bytes(self.buf[since:]))

    def wait_for(self, needle, timeout=8, since=0):
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.2)
            if needle in self.text(since):
                return True
        return False

    def send(self, keys):
        os.write(self.master, keys.encode())

    def alive(self):
        return self.proc.poll() is None

    def quit(self, timeout=6):
        """Presses q. Returns the exit code, or None if it had to be killed."""
        if self.alive():
            self.send("q")
        end = time.time() + timeout
        while time.time() < end and self.alive():
            self.pump(0.1)
        if self.alive():
            self.proc.kill()
            self.proc.wait()
            return None
        self.pump(0.3)
        return self.proc.returncode


def start(tui):
    if not tui.wait_for("WATCHTOP", 12):
        rc = tui.proc.poll()
        raise AssertionError(f"dashboard did not appear (exit code {rc}); output: "
                             + tui.text()[-300:].replace("\n", " "))


def wait_exit(tui, proc, timeout):
    """Waits for a helper to exit while still reading the dashboard output."""
    end = time.time() + timeout
    while time.time() < end:
        tui.pump(0.2)
        if proc.poll() is not None:
            return proc.returncode
    return None


def wait_listed(pid, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        row, _, _ = find_row(pid, tries=1)
        if row:
            return
    raise AssertionError(f"process {pid} is not listed by --once")


def open_kill_dialog(tui, helper):
    tui.send("/")
    tui.pump(0.3)
    tui.send("wt_test_helper")      # search by name: exactly one row matches
    tui.pump(0.6)
    tui.send("\x1b")                # Esc: back to the table
    tui.pump(0.5)
    since = tui.mark()
    tui.send("k")
    if not tui.wait_for("Stop this process?", 5, since):
        raise AssertionError("kill dialog did not open; screen: " + tui.text(since)[-300:].replace("\n", " "))
    assert str(helper.pid) in tui.text(since), "dialog does not show the PID"
    return since


def start_churn(rounds=3):
    """Starts and stops hundreds of short-lived processes, some of them using inotify."""
    script = ("for r in $(seq {rounds}); do "
              "for i in $(seq 200); do sleep 0.01 & done; "
              "for i in $(seq 100); do {py} -c \"import ctypes;ctypes.CDLL('libc.so.6').inotify_init()\" & done; "
              "wait; done").format(rounds=rounds, py=shlex.quote(sys.executable))
    return subprocess.Popen(["bash", "-c", script], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ------------------------------------------------------------------------------ build tests

@test("build: binary exists and is executable")
def t_binary():
    assert BIN.exists(), f"{BIN} not found (build first: cmake --build build -j)"
    assert os.access(BIN, os.X_OK), "file is not executable"


@test("build: Release build type")
def t_build_type():
    if CFG.label:
        raise Skip("special build (label given)")
    cache = BIN.parent / "CMakeCache.txt"
    if not cache.exists():
        raise Skip("CMakeCache.txt not found next to the binary")
    line = next((l for l in cache.read_text().splitlines() if l.startswith("CMAKE_BUILD_TYPE:")), "")
    assert "=Release" in line or "=RelWithDebInfo" in line, f"build type is: {line or 'unset'}"


@test("build: hardening (PIE, BIND_NOW, stack protector)")
def t_hardening():
    if not shutil.which("readelf"):
        raise Skip("readelf not installed")
    header = sh(["readelf", "-h", str(BIN)])
    dynamic = sh(["readelf", "-dW", str(BIN)])
    symbols = sh(["readelf", "-sW", str(BIN)])
    problems = []
    if "DYN" not in header:
        problems.append("not a PIE")
    if "BIND_NOW" not in dynamic:
        problems.append("no BIND_NOW (partial RELRO)")
    if "__stack_chk_fail" not in symbols:
        problems.append("no stack protector")
    assert not problems, ", ".join(problems)


# ------------------------------------------------------------------------------- CLI tests

@test("cli: --version matches CMakeLists.txt")
def t_version():
    p = subprocess.run([str(BIN), "--version"], capture_output=True, text=True, timeout=10)
    assert p.returncode == 0, f"exit code {p.returncode}"
    match = re.search(r"project\(watchtop VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())
    expected = f"watchtop {match.group(1)}" if match else "watchtop "
    assert p.stdout.strip() == expected, f"printed {p.stdout.strip()!r}, expected {expected!r}"
    return p.stdout.strip()


@test("cli: --help")
def t_help():
    p = subprocess.run([str(BIN), "--help"], capture_output=True, text=True, timeout=10)
    assert p.returncode == 0, f"exit code {p.returncode}"
    assert "Usage" in p.stdout and "--once" in p.stdout, "help text is incomplete"


@test("cli: unknown option is rejected with exit code 2")
def t_bad_option():
    p = subprocess.run([str(BIN), "--bogus"], capture_output=True, text=True, timeout=10)
    assert p.returncode == 2, f"exit code {p.returncode}"
    assert p.stderr.strip(), "no error message"


@test("cli: --interval is validated")
def t_interval():
    for bad in ("0", "61", "abc", "-3", ""):
        p = subprocess.run([str(BIN), "--interval", bad, "--once"], capture_output=True, timeout=15)
        assert p.returncode == 2, f"--interval {bad!r} was accepted (exit code {p.returncode})"
    p = subprocess.run([str(BIN), "--interval"], capture_output=True, timeout=15)
    assert p.returncode == 2, f"--interval without a value was accepted (exit code {p.returncode})"
    for good in (["--interval", "1"], ["--interval", "60"], ["--interval=5"]):
        p = subprocess.run([str(BIN)] + good + ["--once"], capture_output=True, timeout=15)
        assert p.returncode == 0, f"{' '.join(good)} was rejected (exit code {p.returncode})"


@test("scan: --once output format and totals")
def t_once_format():
    h = spawn_helper(n=5)
    p = run_once()
    assert p.returncode == 0, f"exit code {p.returncode}"
    info, rows = parse_once(p.stdout)
    assert info.get("banner", "").startswith("#watchtop"), "missing banner line"
    assert int(info["processes"]) == len(rows), "processes= does not match the rows"
    assert int(info["total_watches"]) == sum(r["watches"] for r in rows), "total_watches= does not match the rows"
    assert any(r["pid"] == h.pid for r in rows), "test process is not listed"
    return f"{len(rows)} processes, {info['total_watches']} watches"


# --------------------------------------------------------------------------- accuracy tests

def make_count_test(n, inst):
    def fn():
        h = spawn_helper(n=n, inst=inst)
        expected = n * inst
        if h.ok != expected:
            raise Skip(f"kernel accepted only {h.ok} of {expected} watches (limit reached)")
        real = real_count(h.pid)
        assert real == expected, f"test setup problem: /proc shows {real}, helper created {expected}"
        row, _, _ = find_row(h.pid)
        assert row, "process is not listed"
        assert row["watches"] == real, f"watchtop shows {row['watches']}, /proc shows {real}"
        return f"watchtop={row['watches']} /proc={real}"
    label = f"{n} watches" + (f" in {inst} inotify instances" if inst > 1 else "")
    TESTS.append((f"count: {label} match /proc", fn))


for _n, _inst in ((1, 1), (15, 1), (16, 1), (17, 1), (300, 1), (2000, 1), (50, 3)):
    make_count_test(_n, _inst)


@test("ports: listening TCP port is reported")
def t_ports():
    h = spawn_helper(n=3, port=True)
    row, _, _ = find_row(h.pid)
    assert row, "process is not listed"
    assert str(h.port) in row["ports"], f"port {h.port} not in {row['ports']}"
    return f"port {h.port}"


# ---------------------------------------------------------------------------- security tests

@test("security: terminal escape codes in a process name are neutralized")
def t_escape():
    h = spawn_helper(n=3, evil=1)
    row, raw, _ = find_row(h.pid)
    assert row, "process is not listed"
    assert b"\x1b" not in raw and b"\x07" not in raw, "raw ESC or BEL byte reached the output"
    assert row["name"] == "A?[2JB?]0;HI?", f"unexpected name {row['name']!r}"


@test("security: C1 control characters (UTF-8 U+0080..U+009F) in a name are neutralized")
def t_c1():
    h = spawn_helper(n=3, evil=2)
    row, raw, _ = find_row(h.pid)
    assert row, "process is not listed"
    assert not re.search(rb"\xc2[\x80-\x9f]", raw), "C1 control character reached the output"


@test("security: other users' processes are not listed (normal user)")
def t_isolation():
    if os.geteuid() == 0:
        raise Skip("running as root")
    if subprocess.run(["sudo", "-n", "true"], capture_output=True).returncode != 0:
        raise Skip("passwordless sudo not available")
    h = spawn_helper(n=3, as_root=True)
    p = run_once()
    assert p.returncode == 0
    _, rows = parse_once(p.stdout)
    assert all(r["pid"] != h.pid for r in rows), "a root-owned process is visible to a normal user"


# ---------------------------------------------------------------------------- stability tests

@test("stability: --once survives process churn")
def t_churn_once():
    churn = start_churn()
    runs, bad = 0, []
    try:
        while churn.poll() is None and runs < 40:
            p = run_once()
            runs += 1
            if p.returncode != 0:
                bad.append(f"run {runs}: exit {p.returncode}")
    finally:
        churn.wait()
    assert not bad, "; ".join(bad[:3])
    return f"{runs} runs"


@test("stability: dashboard survives process churn")
def t_churn_tui():
    with Tui() as t:
        start(t)
        churn = start_churn()
        t.pump(8, keep=False)
        alive = t.alive()
        churn.wait()
        t.pump(1.5, keep=False)
        assert alive and t.alive(), "dashboard crashed during churn"
        assert t.quit() == 0, "did not quit cleanly afterwards"


# ------------------------------------------------------------------------ dashboard tests

@test("tui: starts and quits with q")
def t_quit():
    with Tui() as t:
        start(t)
        t.pump(2)
        assert t.alive(), "dashboard exited by itself"
        rc = t.quit()
        assert rc == 0, f"exit code {rc}"


@test("tui: terminal is restored on quit")
def t_restore():
    with Tui() as t:
        start(t)
        t.pump(1)
        t.quit()
        assert b"\x1b[?1049l" in bytes(t.buf), "alternate screen was not left"


@test("tui: letters typed in the search box are not shortcuts")
def t_search_typing():
    with Tui() as t:
        start(t)
        t.send("/")
        t.pump(0.4)
        since = t.mark()
        t.send("qrk")
        t.pump(1.5)
        assert t.alive(), "q in the search box quit the program"
        assert "qrk" in t.text(since), "typed letters did not appear in the search box"
        t.send("\x1b")
        t.pump(0.5)
        assert t.quit() == 0


@test("tui: r shows 'Refreshed!'")
def t_refresh_sign():
    with Tui() as t:
        start(t)
        t.pump(1.5)
        since = t.mark()
        t.send("r")
        assert t.wait_for("Refreshed!", 6, since), "no 'Refreshed!' indicator after pressing r"
        t.quit()


@test("kill: Esc cancels, nothing is sent")
def t_kill_cancel():
    h = spawn_helper(n=5)
    wait_listed(h.pid)
    with Tui() as t:
        start(t)
        open_kill_dialog(t, h)
        t.send("\x1b")
        t.pump(2)
        assert h.proc.poll() is None, "process was stopped although the dialog was cancelled"
        t.quit()


@test("kill: Enter sends SIGTERM")
def t_kill_term():
    h = spawn_helper(n=5, port=True)
    wait_listed(h.pid)
    with Tui() as t:
        start(t)
        since = open_kill_dialog(t, h)
        t.send("\r")
        rc = wait_exit(t, h.proc, 6)
        assert rc == -signal.SIGTERM, f"helper exit status {rc} (expected {-signal.SIGTERM})"
        assert t.wait_for("SIGTERM sent", 4, since), "no confirmation message in the footer"
        t.quit()


@test("kill: f sends SIGKILL when SIGTERM is ignored")
def t_kill_force():
    h = spawn_helper(n=5, port=True, ign=True)
    wait_listed(h.pid)
    with Tui() as t:
        start(t)
        open_kill_dialog(t, h)
        t.send("\r")
        assert wait_exit(t, h.proc, 2.5) is None, "SIGTERM should have been ignored"
        since = t.mark()
        t.send("k")
        assert t.wait_for("Stop this process?", 5, since), "dialog did not reopen after SIGTERM was ignored"
        t.send("f")
        rc = wait_exit(t, h.proc, 6)
        assert rc == -signal.SIGKILL, f"helper exit status {rc} (expected {-signal.SIGKILL})"
        t.quit()


# ---------------------------------------------------------------------------- resource test

def proc_ticks(pid):
    data = Path(f"/proc/{pid}/stat").read_text()
    rest = data[data.rfind(")") + 2:].split()
    return int(rest[11]) + int(rest[12])


def thread_ticks(pid):
    """{thread id: (thread name, cpu ticks)} for every thread of a process."""
    out = {}
    for tid in os.listdir(f"/proc/{pid}/task"):
        try:
            data = Path(f"/proc/{pid}/task/{tid}/stat").read_text()
        except OSError:
            continue
        rest = data[data.rfind(")") + 2:].split()
        out[int(tid)] = (data[data.find("(") + 1:data.rfind(")")], int(rest[11]) + int(rest[12]))
    return out


def rss_kb(pid):
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    return 0


@test("perf: one scan costs little CPU")
def t_scan_cost():
    import resource
    if is_instrumented():
        raise Skip("sanitizer build: it runs 5 to 15 times slower, so CPU numbers mean nothing")
    spawn_helper(n=5)
    run_once()                                   # warm-up
    times = []
    for _ in range(7):
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        p = run_once()
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        assert p.returncode == 0, f"--once exited with {p.returncode}"
        times.append((after.ru_utime - before.ru_utime + after.ru_stime - before.ru_stime) * 1000)
    times.sort()
    median = times[len(times) // 2]
    detail = (f"median {median:.1f} ms per scan, 7 runs (limit {CFG.max_scan_ms} ms); "
              f"one scan per second is about {median / 10:.1f}% CPU")
    assert median <= CFG.max_scan_ms, "scan too slow: " + detail
    return detail


@test("perf: idle CPU and memory")
def t_perf():
    if is_instrumented():
        raise Skip("sanitizer build: it runs 5 to 15 times slower, so CPU numbers mean nothing")
    seconds = CFG.long or 20
    hz = os.sysconf("SC_CLK_TCK")
    with Tui() as t:
        start(t)
        t.pump(3, keep=False)                      # warm-up
        pid = t.proc.pid
        rss0 = rss_kb(pid)
        t_begin = last_time = time.time()
        ticks0 = last_ticks = proc_ticks(pid)
        threads0 = thread_ticks(pid)
        samples = []
        while time.time() - t_begin < seconds:
            t.pump(min(10, seconds), keep=False)
            assert t.alive(), "dashboard died during the run"
            now, ticks = time.time(), proc_ticks(pid)
            samples.append((ticks - last_ticks) * 100.0 / (hz * (now - last_time)))
            last_ticks, last_time = ticks, now
        rss1 = rss_kb(pid)
        avg = (last_ticks - ticks0) * 100.0 / (hz * (last_time - t_begin))
        threads1 = thread_ticks(pid)
        t.quit()
    span = last_time - t_begin
    per_thread = sorted(((n1, (c1 - threads0[tid][1]) * 100.0 / (hz * span))
                         for tid, (n1, c1) in threads1.items() if tid in threads0),
                        key=lambda x: -x[1])
    breakdown = ", ".join(f"{name} {cpu:.1f}%" for name, cpu in per_thread if cpu >= 0.05) or "none above 0.05%"
    detail = (f"{seconds}s: cpu avg {avg:.2f}% max {max(samples):.2f}% (limit {CFG.max_cpu}%), "
              f"rss {rss0 / 1024:.1f} -> {rss1 / 1024:.1f} MB; threads: {breakdown}")
    assert avg <= CFG.max_cpu, "CPU too high: " + detail
    assert rss1 - rss0 <= CFG.max_rss_growth_kb, "memory grew: " + detail
    return detail


# ---------------------------------------------------------------- sanitizer result (runs last)

@test("sanitizer: no reports were written")
def t_sanitizers():
    if not shutil.which("readelf"):
        raise Skip("readelf not installed")
    if not is_instrumented():
        raise Skip("binary is not built with a sanitizer")
    reports = sorted(p.name for p in SANITIZER_LOGS.glob("*")) if SANITIZER_LOGS.exists() else []
    assert not reports, f"{len(reports)} report file(s) in tests/results/sanitizer_logs: {', '.join(reports[:3])}"


# ------------------------------------------------------------------------------- reporting

def environment():
    def first_line(cmd):
        out = sh(cmd).strip()
        return out.splitlines()[0] if out else "?"
    os_name = "?"
    try:
        for line in Path("/etc/os-release").read_text().splitlines():
            if line.startswith("PRETTY_NAME="):
                os_name = line.split("=", 1)[1].strip('"')
    except OSError:
        pass
    try:
        limit = Path("/proc/sys/fs/inotify/max_user_watches").read_text().strip()
    except OSError:
        limit = "?"
    return {
        "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "commit": first_line(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"])
                  + (" (+uncommitted changes)"
                     if sh(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"]).strip() else ""),
        "binary": str(BIN),
        "label": CFG.label,
        "os": os_name,
        "kernel": platform.release(),
        "compiler": first_line(["g++", "--version"]),
        "user": "root" if os.geteuid() == 0 else "normal user",
        "max_user_watches": limit,
        "python": platform.python_version(),
    }


def report_suffix():
    """Part of the report file name: the label, or 'partial' when only some tests were chosen.
    This way a partial run can never overwrite the report of a full run."""
    if CFG.label:
        return f"-{CFG.label}"
    if CFG.only or CFG.skip:
        return "-partial"
    return ""


def write_report(results, meta):
    RESULTS.mkdir(parents=True, exist_ok=True)
    suffix = report_suffix()
    (RESULTS / f"report{suffix}.json").write_text(json.dumps({"meta": meta, "results": results}, indent=2))

    counts = {s: sum(1 for r in results if r["status"] == s) for s in ("PASS", "FAIL", "ERROR", "SKIP")}
    md = [f"# watchtop test report{' (' + CFG.label + ')' if CFG.label else ''}", "",
          f"- **Result:** {counts['PASS']} passed, {counts['FAIL']} failed, "
          f"{counts['ERROR']} errors, {counts['SKIP']} skipped",
          f"- **Date:** {meta['date']}   **Commit:** `{meta['commit']}`   **Binary:** `{meta['binary']}`",
          f"- **System:** {meta['os']}, kernel {meta['kernel']}, {meta['compiler']}",
          f"- **Run as:** {meta['user']}   **max_user_watches:** {meta['max_user_watches']}", "",
          "| # | Test | Result | Time | Details |", "|---|------|--------|------|---------|"]
    for i, r in enumerate(results, 1):
        detail = r["detail"].replace("|", "\\|").replace("\n", " ")[:160]
        md.append(f"| {i} | {r['name']} | **{r['status']}** | {r['seconds']:.1f}s | {detail} |")
    problems = [r for r in results if r["status"] in ("FAIL", "ERROR")]
    if problems:
        md += ["", "## Failures", ""]
        for r in problems:
            md += [f"### {r['name']}", "", "```", r["detail"].strip(), "```", ""]
    (RESULTS / f"report{suffix}.md").write_text("\n".join(md) + "\n")

    history = RESULTS / "history.csv"
    new_file = not history.exists()
    with history.open("a", newline="") as f:
        w = csv.writer(f)
        if new_file:
            w.writerow(["date", "commit", "label", "test", "result", "seconds", "detail"])
        for r in results:
            w.writerow([meta["date"], meta["commit"], CFG.label, r["name"], r["status"],
                        f"{r['seconds']:.1f}", r["detail"].replace("\n", " ")[:200]])
    return counts


def main():
    global BIN, CFG
    ap = argparse.ArgumentParser(description="watchtop automated tests")
    ap.add_argument("--bin", default=str(ROOT / "build" / "watchtop"), help="watchtop binary to test")
    ap.add_argument("--only", default="", help="comma-separated words; run tests whose name contains one")
    ap.add_argument("--skip", default="", help="comma-separated words; skip tests whose name contains one")
    ap.add_argument("--list", action="store_true", help="list tests and exit")
    ap.add_argument("--long", type=int, default=0, metavar="SECONDS", help="length of the CPU/memory test (default 20)")
    ap.add_argument("--max-cpu", type=float, default=6.0, help="CPU %% limit for the idle test at the default 2 s interval (default 6)")
    ap.add_argument("--max-scan-ms", type=float, default=100.0, help="CPU ms allowed for one scan (default 100; depends on how many processes and watches the machine has)")
    ap.add_argument("--max-rss-growth-kb", type=int, default=10240, help="allowed memory growth (default 10 MB)")
    ap.add_argument("--label", default="", help="name for this run, e.g. asan (changes the report file name)")
    CFG = ap.parse_args()
    BIN = Path(CFG.bin).resolve()

    only = [w for w in CFG.only.split(",") if w]
    skip = [w for w in CFG.skip.split(",") if w]
    selected = [(n, f) for n, f in TESTS
                if (not only or any(w in n for w in only)) and not any(w in n for w in skip)]

    if CFG.list:
        for name, _ in selected:
            print(name)
        return 0
    if not BIN.exists():
        print(f"error: {BIN} not found. Build first: cmake --build build -j", file=sys.stderr)
        return 2

    try:
        probe = subprocess.run([str(BIN), "--version"], capture_output=True, text=True, timeout=5, env=tool_env())
        understood = probe.returncode == 0 and probe.stdout.startswith("watchtop ")
    except subprocess.TimeoutExpired:
        understood = False
    if not understood:
        print("error: this binary does not understand --version (it opened the dashboard instead).\n"
              "It is an OLD build. Check that src/main.cpp is the new one, then rebuild:\n"
              "    cmake -S . -B build && cmake --build build -j\n"
              f"binary: {BIN}", file=sys.stderr)
        return 2

    if SANITIZER_LOGS.exists():
        shutil.rmtree(SANITIZER_LOGS)
    SANITIZER_LOGS.mkdir(parents=True, exist_ok=True)
    if shutil.which("pkill"):
        subprocess.run(["pkill", "-x", "wt_test_helper"], capture_output=True)   # leftovers of older runs

    color = sys.stdout.isatty()
    paint = {"PASS": "32", "FAIL": "31", "ERROR": "31", "SKIP": "33"}
    meta = environment()
    results = []
    print(f"watchtop tests: {len(selected)} tests, binary {BIN}\n")

    try:
        for name, fn in selected:
            start_time = time.time()
            try:
                detail = fn() or ""
                status = "PASS"
            except Skip as e:
                status, detail = "SKIP", str(e)
            except AssertionError as e:
                status, detail = "FAIL", str(e) or "assertion failed"
            except subprocess.TimeoutExpired as e:
                status, detail = "FAIL", f"command timed out after {e.timeout}s: {' '.join(str(x) for x in e.cmd)[:120]}"
            except Exception:
                status, detail = "ERROR", traceback.format_exc()
            finally:
                cleanup_helpers()
            seconds = time.time() - start_time
            results.append({"name": name, "status": status, "seconds": seconds, "detail": detail})
            tag = f"\033[{paint[status]}m{status:<5}\033[0m" if color else f"{status:<5}"
            line = detail.strip().splitlines()[-1] if detail.strip() else ""
            print(f"{tag} {name} ({seconds:.1f}s){'  ' + line[:110] if line else ''}", flush=True)
    except KeyboardInterrupt:
        print("\ninterrupted, writing partial report")
        cleanup_helpers()

    counts = write_report(results, meta)
    suffix = report_suffix()
    print(f"\n{counts['PASS']} passed, {counts['FAIL']} failed, {counts['ERROR']} errors, {counts['SKIP']} skipped")
    print(f"report: tests/results/report{suffix}.md  (also .json, history.csv)")
    return 1 if counts["FAIL"] or counts["ERROR"] else 0


if __name__ == "__main__":
    sys.exit(main())