# Copyright (C) 2026 iceman50
"""Sample a running Windows client's responsiveness and process I/O without injection."""
import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import time


class IoCounters(c.Structure):
    _fields_ = [(name, c.c_ulonglong) for name in
                ("read_operations", "write_operations", "other_operations", "read_bytes", "write_bytes", "other_bytes")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--seconds", type=int, default=45)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 300 or args.pid <= 0:
        parser.error("seconds must be 1..300 and pid must be positive")
    if not args.label or any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for ch in args.label):
        parser.error("label must use letters, digits, underscores, or hyphens")
    root = Path(__file__).resolve().parent.parent
    output = root / "build/live-transfer-profile" / args.label
    output.mkdir(parents=True, exist_ok=False)
    u = c.WinDLL("user32", use_last_error=True)
    k = c.WinDLL("kernel32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.EnumWindows.argtypes = [callback_type, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    u.IsWindowVisible.argtypes = [w.HWND]
    u.IsIconic.argtypes = [w.HWND]
    u.GetForegroundWindow.restype = w.HWND
    u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    u.GetWindow.argtypes = [w.HWND, w.UINT]
    u.GetWindow.restype = w.HWND
    u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
    u.SendMessageTimeoutW.restype = w.LPARAM
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.CloseHandle.argtypes = [w.HANDLE]
    k.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
    k.GetProcessIoCounters.argtypes = [w.HANDLE, c.POINTER(IoCounters)]
    k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    windows = []

    @callback_type
    def visit(hwnd, unused):
        pid = w.DWORD()
        tid = u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == args.pid and not u.GetWindow(hwnd, 4):  # GW_OWNER
            name = c.create_unicode_buffer(256)
            u.GetClassNameW(hwnd, name, len(name))
            windows.append((bool(u.IsWindowVisible(hwnd)), hwnd, tid, name.value))
        return True

    u.EnumWindows(visit, 0)
    if not windows:
        raise RuntimeError("No client window is accessible; run outside the sandbox in the interactive desktop")
    visible, hwnd, tid, window_class = max(windows)
    handle = k.OpenProcess(0x1000, False, args.pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        raise c.WinError(c.get_last_error())
    rows = []
    try:
        image = c.create_unicode_buffer(32768)
        image_length = w.DWORD(len(image))
        if not k.QueryFullProcessImageNameW(handle, 0, image, c.byref(image_length)):
            raise c.WinError(c.get_last_error())
        metadata = dict(pid=args.pid, thread_id=tid, window_class=window_class, executable=image.value,
                        start_utc=datetime.now(timezone.utc).isoformat(), seconds=args.seconds,
                        note="WM_NULL response is a UI-thread responsiveness proxy, not input-to-paint or network latency. Process I/O includes cached and non-disk activity.")
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        started = time.perf_counter()
        previous = None
        while time.perf_counter() - started < args.seconds:
            t = time.perf_counter()
            pid = w.DWORD()
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value != args.pid:
                raise RuntimeError("Target window closed or changed owner")
            creation, exit_time, kernel, user = (w.FILETIME() for _ in range(4))
            io = IoCounters()
            if not k.GetProcessTimes(handle, c.byref(creation), c.byref(exit_time), c.byref(kernel), c.byref(user)) or not k.GetProcessIoCounters(handle, c.byref(io)):
                raise c.WinError(c.get_last_error())
            cpu = sum((v.dwHighDateTime << 32) | v.dwLowDateTime for v in (kernel, user)) / 1e7
            reply = c.c_size_t()
            c.set_last_error(0)
            response_start = time.perf_counter()
            ok = bool(u.SendMessageTimeoutW(hwnd, 0, 0, 0, 3, 200, c.byref(reply)))
            response_ms = (time.perf_counter() - response_start) * 1000
            error = 0 if ok else c.get_last_error()
            row = dict(elapsed_s=t-started, response_ms=response_ms, response_ok=int(ok), error=error,
                       visible=int(bool(u.IsWindowVisible(hwnd))), minimized=int(bool(u.IsIconic(hwnd))),
                       foreground=int(u.GetForegroundWindow() == hwnd), cpu_one_core_percent=0.0,
                       read_bytes_per_s=0.0, write_bytes_per_s=0.0, other_bytes_per_s=0.0)
            if previous:
                dt = t - previous[0]
                row.update(cpu_one_core_percent=100*(cpu-previous[1])/dt,
                           read_bytes_per_s=(io.read_bytes-previous[2])/dt,
                           write_bytes_per_s=(io.write_bytes-previous[3])/dt,
                           other_bytes_per_s=(io.other_bytes-previous[4])/dt)
            previous = t, cpu, io.read_bytes, io.write_bytes, io.other_bytes
            rows.append(row)
            time.sleep(max(0.0, 0.1-(time.perf_counter()-t)))
    finally:
        k.CloseHandle(handle)
        if rows:
            with (output / "samples.csv").open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
    times = sorted(r["response_ms"] for r in rows if r["response_ok"])
    summary = dict(samples=len(rows), failures=sum(not r["response_ok"] for r in rows),
                   visible_samples=sum(r["visible"] and not r["minimized"] for r in rows),
                   response_timeout_ms=200, timing_population="successful responses only")
    if times:
        summary.update(median_ms=statistics.median(times), p95_ms=times[min(len(times)-1, int(len(times)*0.95))],
                       p99_ms=times[min(len(times)-1, int(len(times)*0.99))], max_ms=max(times))
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
