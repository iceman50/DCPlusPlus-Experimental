"""Exercise the built application's theme dropdown using a disposable local profile."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/release-mingw-x64/bin/DCPlusPlus.exe")
    parser.add_argument("--snapshot", type=Path, help="Save a visible rebar screenshot during the isolated test")
    parser.add_argument("--maximized", action="store_true", help="Exercise the maximized main-window layout")
    parser.add_argument("--toolbar-size", type=int, default=48)
    parser.add_argument("--icon-pack", type=Path, help="Use this icon pack in the disposable profile")
    args = parser.parse_args()
    user = c.WinDLL("user32", use_last_error=True)
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback, w.LPARAM]
    user.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = c.c_ssize_t
    user.GetParent.argtypes = [w.HWND]
    user.GetParent.restype = w.HWND
    user.GetDlgCtrlID.argtypes = [w.HWND]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.ShowWindow.argtypes = [w.HWND, c.c_int]
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]

    def rebar_ancestor(hwnd):
        name = c.create_unicode_buffer(128)
        parent = user.GetParent(hwnd)
        while parent:
            user.GetClassNameW(parent, name, len(name))
            if "rebar" in name.value.lower():
                return parent
            parent = user.GetParent(parent)
        return None

    def snapshot(hwnd, path, combo, expected_background, frame=False):
        gdi = c.WinDLL("gdi32")
        user.GetDC.argtypes = [w.HWND]
        user.GetDC.restype = w.HDC
        user.ReleaseDC.argtypes = [w.HWND, w.HDC]
        gdi.CreateCompatibleDC.argtypes = [w.HDC]
        gdi.CreateCompatibleDC.restype = w.HDC
        gdi.CreateDIBSection.argtypes = [w.HDC, c.c_void_p, w.UINT, c.POINTER(c.c_void_p), w.HANDLE, w.DWORD]
        gdi.CreateDIBSection.restype = w.HANDLE
        gdi.SelectObject.argtypes = [w.HDC, w.HANDLE]
        gdi.SelectObject.restype = w.HANDLE
        gdi.BitBlt.argtypes = [w.HDC, c.c_int, c.c_int, c.c_int, c.c_int, w.HDC, c.c_int, c.c_int, w.DWORD]
        gdi.DeleteObject.argtypes = [w.HANDLE]
        gdi.DeleteDC.argtypes = [w.HDC]
        bounds = w.RECT()
        user.GetClientRect(hwnd, c.byref(bounds))
        width, height = bounds.right, bounds.bottom
        origin = w.POINT()
        user.ClientToScreen(hwnd, c.byref(origin))
        bounds.left, bounds.top = origin.x, origin.y
        if frame:
            height = min(height, 180)
        header = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
        dc = user.GetDC(hwnd)
        memory = gdi.CreateCompatibleDC(dc)
        pixels = c.c_void_p()
        bitmap = gdi.CreateDIBSection(dc, c.create_string_buffer(header), 0, c.byref(pixels), None, 0)
        assert bitmap, "Cannot capture rebar"
        previous = gdi.SelectObject(memory, bitmap)
        try:
            assert gdi.BitBlt(memory, 0, 0, width, height, dc, 0, 0, 0x00CC0020), f"Cannot copy rebar pixels: {width}x{height}, DC {dc}, error {c.get_last_error()}"
            gdi.GdiFlush()
            data = c.string_at(pixels, width * height * 4)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(struct.pack("<2sIHHI", b"BM", 54 + len(data), 0, 0, 54) + header + data)
            if not frame:
                windows = []

                @callback
                def describe(child_hwnd, _):
                    name = c.create_unicode_buffer(128)
                    user.GetClassNameW(child_hwnd, name, len(name))
                    rect = w.RECT()
                    user.GetWindowRect(child_hwnd, c.byref(rect))
                    windows.append({"class": name.value, "rect": [rect.left - bounds.left,
                                    rect.top - bounds.top, rect.right - bounds.left,
                                    rect.bottom - bounds.top]})
                    return True

                describe(hwnd, 0)
                user.EnumChildWindows(hwnd, describe, 0)
                path.with_suffix(".json").write_text(json.dumps(windows, indent=2))
            child = w.RECT()
            user.GetWindowRect(combo, c.byref(child))
            y = child.bottom - bounds.top + 2
            if frame:
                band = w.RECT()
                user.GetWindowRect(rebar_ancestor(combo), c.byref(band))
                y = band.bottom - bounds.top + 4
            for x in (child.left - bounds.left - 15, child.left - bounds.left + 5):
                assert 0 <= x < width and 0 <= y < height, "Missing background strip around theme selector"
                blue, green, red = data[(y * width + x) * 4:(y * width + x) * 4 + 3]
                assert red | (green << 8) | (blue << 16) == expected_background, "Wrong background below theme label/dropdown"
            if not frame:
                # Probe clear space inside the closed combo, away from its text,
                # border and arrow: it must blend into the surrounding rebar too.
                x = child.right - bounds.left - 40
                y = (child.top + child.bottom) // 2 - bounds.top
                assert 0 <= x < width and 0 <= y < height, "Missing theme dropdown interior"
                blue, green, red = data[(y * width + x) * 4:(y * width + x) * 4 + 3]
                assert red | (green << 8) | (blue << 16) == expected_background, "Theme dropdown does not match the rebar background"
                # Check the entire exposed strip below the host, not just two
                # pixels that could miss a child-sized patch of stale paint.
                host = w.RECT()
                user.GetWindowRect(user.GetParent(combo), c.byref(host))
                for y in range(max(child.bottom, host.bottom) - bounds.top + 1, height):
                    for x in range(max(0, host.left - bounds.left - 12), min(width, host.right - bounds.left)):
                        blue, green, red = data[(y * width + x) * 4:(y * width + x) * 4 + 3]
                        assert red | (green << 8) | (blue << 16) == expected_background, f"Wrong rebar strip background at ({x}, {y})"
        finally:
            gdi.SelectObject(memory, previous)
            gdi.DeleteObject(bitmap)
            gdi.DeleteDC(memory)
            user.ReleaseDC(hwnd, dc)

    def wait_for(check, process):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise AssertionError(f"Application exited unexpectedly: {process.returncode}")
            result = check()
            if result:
                return result
            time.sleep(0.1)
        raise AssertionError("Timed out waiting for the theme selector")

    def find_selector(process):
        found = []

        @callback
        def child(hwnd, _):
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if "ComboBox" in name.value and user.SendMessageW(hwnd, 0x146, 0, 0) > 4:
                if rebar_ancestor(hwnd):
                    found.append(hwnd)
            return True

        @callback
        def window(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value == process.pid:
                before = len(found)
                user.EnumChildWindows(hwnd, child, 0)
                if len(found) > before:
                    found.append(hwnd)
                    return False
            return True

        user.EnumWindows(window, 0)
        return (found[0], found[-1]) if found else None

    scratch = ROOT / "build/test-theme-selector"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        folder = Path(directory)
        shutil.copy2(args.exe, folder / "DCPlusPlus.exe")
        shutil.copytree(ROOT / "Themes", folder / "Themes")
        (folder / "profile").mkdir()
        (folder / "dcppboot.xml").write_text("<Boot><LocalMode>1</LocalMode><ConfigPath>profile\\</ConfigPath></Boot>")
        config = folder / "profile/DCPlusPlus.xml"
        config.write_text(f'''<DCPlusPlus><Settings>
<ConfigVersion type="string">0.8842</ConfigVersion><Nick type="string">Theme selector test</Nick>
<ConfirmExit type="int">0</ConfirmExit><ShowToolbar type="int">1</ShowToolbar>
<ToolbarSize type="int">{args.toolbar_size}</ToolbarSize>
<MainFont type="string">Segoe UI,-24,400,0,1</MainFont>
<ThemeMode type="int">1</ThemeMode><TestingStatus type="int">0</TestingStatus>
</Settings><Windows><Window Id="SystemLog" /></Windows></DCPlusPlus>''')
        if args.icon_pack:
            pack = folder / args.icon_pack.name
            shutil.copy2(args.icon_pack, pack)
            document = ET.parse(config)
            ET.SubElement(document.find("Settings"), "IconPack", type="string").text = str(pack)
            document.write(config, encoding="utf-8", xml_declaration=True)

        def settings():
            try:
                return ET.parse(config).find("Settings")
            except (OSError, ET.ParseError):
                return None

        for restart in (False, True):
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = (3 if args.maximized else 4) if args.snapshot else 0
            process = subprocess.Popen([str(folder / "DCPlusPlus.exe")], cwd=folder, startupinfo=startup)
            window = None
            try:
                combo, window = wait_for(lambda: find_selector(process), process)
                parent = rebar_ancestor(combo)
                name = c.create_unicode_buffer(128)
                user.GetClassNameW(parent, name, len(name))
                assert "rebar" in name.value.lower(), "Theme selector is outside the rebar"
                bounds = w.RECT()
                user.GetWindowRect(combo, c.byref(bounds))
                assert bounds.right > bounds.left and bounds.bottom > bounds.top, "Empty theme selector bounds"
                if restart:
                    assert user.SendMessageW(combo, 0x147, 0, 0) == 8, "Saved theme was not recognized after restart"
                    if args.snapshot:
                        time.sleep(1)
                        snapshot(parent, args.snapshot.with_name("startup-" + args.snapshot.name), combo,
                                 int(settings().findtext("ThemeBackgroundColor")))
                        snapshot(window, args.snapshot.with_name("startup-frame-" + args.snapshot.name), combo,
                                 int(settings().findtext("ThemeBackgroundColor")), frame=True)
                else:
                    if args.snapshot:
                        user.ShowWindow(window, 3 if args.maximized else 4)
                        if not args.maximized:
                            user.SetWindowPos(window, None, 30, 30, 1800, 700, 0x10)
                        time.sleep(0.5)
                    ultraviolet = c.create_unicode_buffer("Ultraviolet")
                    ultraviolet_index = user.SendMessageW(combo, 0x158, -1, c.addressof(ultraviolet))  # CB_FINDSTRINGEXACT
                    assert ultraviolet_index >= 0, "Missing Ultraviolet test theme"
                    for index, expected in ((1, 0x271811), (2, 0x181818), (0, None), (4, None),
                                            (ultraviolet_index, 0x1B0A0B), (8, 0x221507)):
                        previous = settings().findtext("ThemeBackgroundColor")
                        user.SendMessageW(combo, 0x14F, 1, 0)  # CB_SHOWDROPDOWN
                        user.SendMessageW(combo, 0x14E, index, 0)  # CB_SETCURSEL
                        user.SendMessageW(combo, 0x100, 0x0D, 0)  # Confirm with Enter through the native control.

                        def applied():
                            saved = settings()
                            if saved is None or saved.findtext("ThemeMode") != "2":
                                return False
                            color = saved.findtext("ThemeBackgroundColor")
                            return color == str(expected) if expected is not None else color != previous

                        wait_for(applied, process)
                        assert user.SendMessageW(combo, 0x147, 0, 0) == index, "Applying a theme lost the combo selection"
                        if args.snapshot and index in (1, 4, ultraviolet_index, 8):
                            time.sleep(1)
                            path = args.snapshot if index == 8 else args.snapshot.with_name("first-" + args.snapshot.name)
                            snapshot(parent, path, combo, int(settings().findtext("ThemeBackgroundColor")))
                            snapshot(window, args.snapshot.with_name(f"frame-{index}-" + args.snapshot.name), combo,
                                     int(settings().findtext("ThemeBackgroundColor")), frame=True)
                    user.SendMessageW(combo, 0x14F, 1, 0)
                    user.SendMessageW(combo, 0x100, 0x1B, 0)  # Cancel with Escape.
                    assert user.SendMessageW(combo, 0x147, 0, 0) == 8, "Cancelling the dropdown changed the theme"
                    # Removing the selector from layout must not remove a phantom
                    # extra row below it. Only compare a shared row: on narrow
                    # windows the selector may legitimately wrap to a second row.
                    height_with_selector = user.SendMessageW(parent, 0x41B, 0, 0)  # RB_GETBARHEIGHT
                    row_count = user.SendMessageW(parent, 0x40D, 0, 0)  # RB_GETROWCOUNT
                    try:
                        assert user.SendMessageW(parent, 0x423, 1, 0), "Cannot hide theme band"  # RB_SHOWBAND
                        height_without_selector = user.SendMessageW(parent, 0x41B, 0, 0)
                        if row_count == 1:
                            assert height_with_selector == height_without_selector, "Theme band adds unexpected rebar height"
                    finally:
                        user.SendMessageW(parent, 0x423, 1, 1)
                    print(f"Rebar height with/without theme band: {height_with_selector}/{height_without_selector}px ({row_count} rows).")
                    print("Built-in and installed themes applied and saved through the rebar dropdown.")
                user.PostMessageW(window, 0x10, 0, 0)
                assert process.wait(timeout=30) == 0, "Application failed to shut down normally"
            finally:
                if process.poll() is None:
                    if window:
                        user.PostMessageW(window, 0x10, 0, 0)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        process.wait(timeout=10)
        print("Theme selection survived restart; isolated application smoke test passed.")


if __name__ == "__main__":
    main()
