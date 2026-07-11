"""rAlt: a minimal, rcmd-inspired application switcher for Windows."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import string
import sys
import threading
from dataclasses import dataclass

import dearpygui.dearpygui as dpg
import keyboard
import pystray
from PIL import Image, ImageDraw


APP_NAME = "rAlt"
VIEWPORT_TITLE = "ralt_overlay"
WINDOW_TAG = "ralt_window"
LIST_TAG = "app_list"
APP_DIR = Path(__file__).resolve().parent
CONFIG_PATH = APP_DIR / "ralt_config.json"
VIEWPORT_WIDTH = 360

GA_ROOTOWNER = 3
GWL_EXSTYLE = -20
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_APPWINDOW = 0x00040000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
SW_HIDE = 0
SW_SHOW = 5
SW_RESTORE = 9
MONITOR_DEFAULTTONEAREST = 2

DEFAULT_CONFIG = {
    "app_overrides": {
        "msedge.exe": "Edge",
        "chrome.exe": "Chrome",
        "rider64.exe": "Rider",
        "code.exe": "VS Code",
        "devenv.exe": "Visual Studio",
        "explorer.exe": "Explorer",
        "notepad.exe": "Notepad",
        "cmd.exe": "Command Prompt",
        "powershell.exe": "PowerShell",
        "windowsterminal.exe": "Terminal",
        "wt.exe": "Terminal",
    },
    "letter_overrides": {},
}


def enable_dpi_awareness() -> None:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except (AttributeError, OSError):
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass


if sys.platform != "win32":
    raise SystemExit("rAlt only runs on Windows.")

enable_dpi_awareness()
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32


class RECT(ctypes.Structure):
    _fields_ = [("left", wintypes.LONG), ("top", wintypes.LONG),
                ("right", wintypes.LONG), ("bottom", wintypes.LONG)]


class POINT(ctypes.Structure):
    _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


class MONITORINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("rcMonitor", RECT),
                ("rcWork", RECT), ("dwFlags", wintypes.DWORD)]


@dataclass(frozen=True, slots=True)
class WindowEntry:
    hwnd: int
    title: str
    exe: str
    app_name: str
    letter: str


def load_config() -> dict:
    config = {key: dict(value) for key, value in DEFAULT_CONFIG.items()}
    try:
        saved = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        for key in config:
            if isinstance(saved.get(key), dict):
                config[key].update(saved[key])
    except FileNotFoundError:
        save_config(config)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"Could not load {CONFIG_PATH.name}: {exc}", file=sys.stderr)
    return config


def save_config(config: dict) -> None:
    try:
        CONFIG_PATH.write_text(json.dumps(config, indent=4) + "\n", encoding="utf-8")
    except OSError as exc:
        print(f"Could not save {CONFIG_PATH.name}: {exc}", file=sys.stderr)


class RAlt:
    def __init__(self) -> None:
        self.config = load_config()
        self.viewport_hwnd = 0
        self.tray_icon: pystray.Icon | None = None
        self.trigger_held = False
        self.groups: dict[str, list[WindowEntry]] = {}
        self.indices: dict[str, int] = {}
        self.recent_hwnds: list[int] = []
        self.letter_hooks: list = []
        self.state_lock = threading.Lock()

    @staticmethod
    def window_text(hwnd: int) -> str:
        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return ""
        buffer = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buffer, length + 1)
        return buffer.value.strip()

    def is_switchable(self, hwnd: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return False
        title = self.window_text(hwnd)
        if not title or title == VIEWPORT_TITLE:
            return False
        if user32.GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW:
            return False
        return user32.GetAncestor(hwnd, GA_ROOTOWNER) == hwnd

    @staticmethod
    def process_name(hwnd: int) -> str:
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if not pid.value:
            return ""
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
        if not handle:
            return ""
        try:
            size = wintypes.DWORD(32768)
            buffer = ctypes.create_unicode_buffer(size.value)
            if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                return ""
            return os.path.basename(buffer.value)
        finally:
            kernel32.CloseHandle(handle)

    def app_name(self, exe_name: str, title: str) -> str:
        exe = exe_name.lower()
        if exe == "applicationframehost.exe":
            return "Settings" if "settings" in title.lower() else (title or "Windows App")
        if exe == "chrome.exe" and "chatgpt" in title.lower():
            return "ChatGPT"
        override = self.config["app_overrides"].get(exe)
        if override:
            return override
        name = Path(exe_name).stem.removesuffix("64")
        return name[:1].upper() + name[1:]

    def enumerate_windows(self) -> list[WindowEntry]:
        entries: list[WindowEntry] = []
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

        def callback(hwnd, _lparam):
            if self.is_switchable(hwnd):
                title = self.window_text(hwnd)
                exe = self.process_name(hwnd)
                if exe:
                    name = self.app_name(exe, title)
                    custom = self.config["letter_overrides"].get(name, "")
                    letter = custom.lower() if len(custom) == 1 else name[:1].lower()
                    if letter in string.ascii_lowercase:
                        entries.append(WindowEntry(hwnd, title, exe, name, letter))
            return True

        user32.EnumWindows(callback_type(callback), 0)
        recent_rank = {hwnd: rank for rank, hwnd in enumerate(self.recent_hwnds)}
        entries.sort(key=lambda entry: (entry.letter, recent_rank.get(entry.hwnd, 10_000), entry.app_name.lower()))
        return entries

    def refresh(self) -> None:
        self.groups.clear()
        for entry in self.enumerate_windows():
            self.groups.setdefault(entry.letter, []).append(entry)
        self.indices = {letter: 0 for letter in self.groups}

    def update_ui(self) -> None:
        dpg.delete_item(LIST_TAG, children_only=True)
        if not self.groups:
            dpg.add_text("No windows", parent=LIST_TAG, color=(150, 150, 150))
            return
        for letter, entries in self.groups.items():
            names = []
            for entry in entries:
                if entry.app_name not in names:
                    names.append(entry.app_name)
            dpg.add_text(f"{letter.upper()}   {' / '.join(names)}", parent=LIST_TAG)

    def center_overlay(self, height: int) -> None:
        point = POINT()
        user32.GetCursorPos(ctypes.byref(point))
        monitor = user32.MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST)
        info = MONITORINFO(cbSize=ctypes.sizeof(MONITORINFO))
        user32.GetMonitorInfoW(monitor, ctypes.byref(info))
        area = info.rcWork
        height = min(height, int((area.bottom - area.top) * 0.85))
        x = area.left + ((area.right - area.left) - VIEWPORT_WIDTH) // 2
        y = area.top + ((area.bottom - area.top) - height) // 2
        dpg.set_viewport_pos((x, y))
        dpg.set_viewport_height(height)
        user32.SetWindowPos(self.viewport_hwnd, -1, x, y, VIEWPORT_WIDTH, height, 0)

    def show_overlay(self) -> None:
        self.refresh()
        self.update_ui()
        dpg.render_dearpygui_frame()
        height = int(dpg.get_item_rect_size("content")[1]) + 24
        self.center_overlay(height)
        user32.ShowWindow(self.viewport_hwnd, SW_SHOW)

    def hide_overlay(self) -> None:
        if self.viewport_hwnd:
            user32.ShowWindow(self.viewport_hwnd, SW_HIDE)

    @staticmethod
    def activate(hwnd: int) -> None:
        if not user32.IsWindow(hwnd):
            return
        if user32.IsIconic(hwnd):
            user32.ShowWindow(hwnd, SW_RESTORE)
        foreground = user32.GetForegroundWindow()
        foreground_thread = user32.GetWindowThreadProcessId(foreground, None) if foreground else 0
        current_thread = kernel32.GetCurrentThreadId()
        attached = foreground_thread not in (0, current_thread)
        if attached:
            user32.AttachThreadInput(current_thread, foreground_thread, True)
        try:
            user32.BringWindowToTop(hwnd)
            user32.SetForegroundWindow(hwnd)
        finally:
            if attached:
                user32.AttachThreadInput(current_thread, foreground_thread, False)

    def select(self, letter: str) -> None:
        group = self.groups.get(letter)
        if not group:
            return
        index = self.indices.get(letter, 0) % len(group)
        entry = group[index]
        self.indices[letter] = (index + 1) % len(group)
        self.recent_hwnds = [entry.hwnd] + [hwnd for hwnd in self.recent_hwnds if hwnd != entry.hwnd]
        self.recent_hwnds = self.recent_hwnds[:100]
        self.hide_overlay()
        self.activate(entry.hwnd)

    def install_letter_hooks(self) -> None:
        if self.letter_hooks:
            return
        for letter in string.ascii_lowercase:
            hook = keyboard.on_press_key(letter, lambda _event, ch=letter: self.select(ch), suppress=True)
            self.letter_hooks.append(hook)
        self.letter_hooks.append(keyboard.on_press_key("esc", lambda _event: self.trigger_up(), suppress=True))

    def remove_letter_hooks(self) -> None:
        for hook in self.letter_hooks:
            keyboard.unhook(hook)
        self.letter_hooks.clear()

    def trigger_down(self, _event=None) -> None:
        with self.state_lock:
            if self.trigger_held:
                return
            self.trigger_held = True
            self.show_overlay()
            self.install_letter_hooks()

    def trigger_up(self, _event=None) -> None:
        with self.state_lock:
            if not self.trigger_held:
                return
            self.trigger_held = False
            self.hide_overlay()
            self.remove_letter_hooks()

    def keyboard_worker(self) -> None:
        # keyboard.on_press_key("right alt") also registers Left Alt on
        # Windows because the library expands that name to scan code 56.
        # Filtering decoded events keeps Left Alt completely untouched.
        def handle_trigger(event: keyboard.KeyboardEvent) -> None:
            if event.name != "right alt":
                return
            if event.event_type == keyboard.KEY_DOWN:
                self.trigger_down(event)
            elif event.event_type == keyboard.KEY_UP:
                self.trigger_up(event)

        keyboard.hook(handle_trigger)
        keyboard.wait()

    @staticmethod
    def tray_image() -> Image.Image:
        image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rounded_rectangle((3, 3, 29, 29), radius=6, fill=(38, 38, 38, 255))
        draw.text((11, 7), "R", fill="white")
        return image

    def open_config(self, _icon=None, _item=None) -> None:
        if not CONFIG_PATH.exists():
            save_config(self.config)
        os.startfile(CONFIG_PATH)

    def quit(self, icon=None, _item=None) -> None:
        keyboard.unhook_all()
        dpg.stop_dearpygui()
        if icon:
            icon.stop()

    def setup_tray(self) -> None:
        self.tray_icon = pystray.Icon(
            "ralt", self.tray_image(), APP_NAME,
            pystray.Menu(pystray.MenuItem("Edit config", self.open_config),
                         pystray.MenuItem("Quit", self.quit)),
        )
        self.tray_icon.run_detached()

    def setup_ui(self) -> None:
        dpg.create_context()
        with dpg.font_registry():
            font_path = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "segoeui.ttf"
            if font_path.exists():
                dpg.bind_font(dpg.add_font(str(font_path), 17))
        with dpg.window(tag=WINDOW_TAG, no_scrollbar=True):
            with dpg.group(tag="content"):
                dpg.add_text(APP_NAME, color=(140, 180, 255))
                dpg.add_separator()
                dpg.add_group(tag=LIST_TAG)
        dpg.create_viewport(title=VIEWPORT_TITLE, width=VIEWPORT_WIDTH, height=200,
                            decorated=False, resizable=False, always_on_top=True)
        dpg.setup_dearpygui()
        dpg.show_viewport()
        dpg.set_primary_window(WINDOW_TAG, True)
        dpg.render_dearpygui_frame()
        self.viewport_hwnd = user32.FindWindowW(None, VIEWPORT_TITLE)
        if self.viewport_hwnd:
            user32.ShowWindow(self.viewport_hwnd, SW_HIDE)
            style = user32.GetWindowLongW(self.viewport_hwnd, GWL_EXSTYLE)
            user32.SetWindowLongW(self.viewport_hwnd, GWL_EXSTYLE,
                                  (style | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW)

    def run(self) -> None:
        self.setup_ui()
        self.setup_tray()
        threading.Thread(target=self.keyboard_worker, name="keyboard-hook", daemon=True).start()
        try:
            while dpg.is_dearpygui_running():
                dpg.render_dearpygui_frame()
        finally:
            keyboard.unhook_all()
            if self.tray_icon:
                self.tray_icon.stop()
            dpg.destroy_context()


if __name__ == "__main__":
    RAlt().run()
