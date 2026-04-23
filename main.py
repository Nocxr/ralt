import ctypes
import sys

# --- ENABLE DPI AWARENESS EARLY ---
if sys.platform == 'win32':
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2) # PROCESS_PER_MONITOR_DPI_AWARE
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass

import threading
import os
import string
import json

import dearpygui.dearpygui as dpg
import keyboard
import pystray

from PIL import Image, ImageDraw

VIEWPORT_TITLE = "ralt_overlay"
WINDOW_TAG = "ralt_window"
CONFIG_FILE = "ralt_config.json"

VIEWPORT_WIDTH = 500

right_alt_held = False
viewport_hwnd = None
tray_icon = None

current_letter_groups = {}
current_letter_order = []
current_letter_indices = {}

letter_hooks = []  

config = {
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
        "wt.exe": "Terminal"
    },
    "letter_overrides": {}
}

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

GA_ROOTOWNER = 3
GWL_EXSTYLE = -20
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_APPWINDOW = 0x00040000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
SW_RESTORE = 9

SW_HIDE = 0
SW_SHOW = 5

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

class MONITORINFO(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_ulong), ("rcMonitor", RECT),
                ("rcWork", RECT), ("dwFlags", ctypes.c_ulong)]


def load_config():
    global config
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as f:
                saved_config = json.load(f)
                
                for key in ["app_overrides", "letter_overrides"]:
                    if key in saved_config:
                        config[key].update(saved_config[key])
        except Exception as e:
            print(f"Error loading config: {e}")
            
    save_config()


def save_config():
    try:
        with open(CONFIG_FILE, "w") as f:
            json.dump(config, f, indent=4)
    except Exception as e:
        print(f"Error saving config: {e}")


def get_hwnd(title: str):
    return user32.FindWindowW(None, title)


def center_viewport(hwnd, width, height):
    """Centers the window perfectly and applies the dynamic height."""
    pt = POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    monitor = user32.MonitorFromPoint(pt, 2) 
    
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    user32.GetMonitorInfoW(monitor, ctypes.byref(mi))
    
    mon_w = mi.rcMonitor.right - mi.rcMonitor.left
    mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top
    
    max_h = int(mon_h * 0.85)
    if height > max_h:
        height = max_h
    
    x = int(mi.rcMonitor.left + (mon_w - width) / 2)
    y = int(mi.rcMonitor.top + (mon_h - height) / 2)
    
    try:
        dpg.set_viewport_pos([x, y])
        dpg.set_viewport_height(height)
    except Exception:
        pass
    
    user32.SetWindowPos(hwnd, -1, x, y, width, height, 0)


def apply_toolwindow_style(hwnd):
    user32.ShowWindow(hwnd, SW_HIDE)
    exstyle = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    exstyle |= WS_EX_TOOLWINDOW
    exstyle &= ~WS_EX_APPWINDOW
    user32.SetWindowLongW(hwnd, GWL_EXSTYLE, exstyle)


def fetch_window_list():
    global current_letter_groups, current_letter_order

    entries = get_alt_tab_app_entries()
    current_letter_groups = {}
    current_letter_order = []

    for entry in entries:
        letter = entry["letter"]
        if letter not in current_letter_groups:
            current_letter_groups[letter] = []
            current_letter_order.append(letter)
        current_letter_groups[letter].append(entry)


def on_app_action(sender, app_data, user_data):
    print(f"Action button clicked for: {user_data}")
    # Placeholder for future button logic


def update_ui():
    """Dynamically builds rows of text and buttons."""
    global current_letter_groups, current_letter_order, current_letter_indices

    dpg.delete_item("app_list_group", children_only=True)

    if not current_letter_order:
        dpg.add_text("No apps found", parent="app_list_group")
        return

    for idx, letter in enumerate(current_letter_order):
        if idx > 0:
            dpg.add_separator(parent="app_list_group")

        group = current_letter_groups[letter]
        dpg.add_text(f"{letter.upper()}  -", parent="app_list_group")

        current_idx = current_letter_indices.get(letter, 0)
        for i, entry in enumerate(group):
            marker = ">>" if i == current_idx else "  "
            
            with dpg.group(horizontal=True, parent="app_list_group"):
                dpg.add_text(f"    {marker} {entry['app_name']}")
                dpg.add_button(
                    label="⚙ Options", 
                    user_data=entry['exe'], 
                    callback=on_app_action
                )


def show_native_window():
    global viewport_hwnd, current_letter_indices

    if viewport_hwnd is None:
        viewport_hwnd = get_hwnd(VIEWPORT_TITLE)

    current_letter_indices.clear()
    fetch_window_list()             
    update_ui()                     

    # Render one frame invisibly to let ImGui calculate the layout
    dpg.render_dearpygui_frame()
    
    # Measure the height of the master group, plus a 30px bottom margin
    target_height = int(dpg.get_item_rect_size("master_group")[1]) + 30

    if viewport_hwnd:
        center_viewport(viewport_hwnd, VIEWPORT_WIDTH, target_height)
        user32.ShowWindow(viewport_hwnd, SW_SHOW)
        user32.UpdateWindow(viewport_hwnd)


def hide_native_window():
    global viewport_hwnd

    if viewport_hwnd is None:
        viewport_hwnd = get_hwnd(VIEWPORT_TITLE)

    if viewport_hwnd:
        user32.ShowWindow(viewport_hwnd, SW_HIDE)


# --- DYNAMIC KEYBOARD HOOKING ---

def make_letter_callback(letter):
    def callback(event):
        select_app_by_letter(letter)
    return callback

def on_esc_down(event):
    on_right_alt_up(None)

def enable_letter_hooks():
    global letter_hooks
    if not letter_hooks:
        for ch in string.ascii_lowercase:
            h = keyboard.on_press_key(ch, make_letter_callback(ch), suppress=True)
            letter_hooks.append(h)
        h_esc = keyboard.on_press_key("esc", on_esc_down, suppress=True)
        letter_hooks.append(h_esc)

def disable_letter_hooks():
    global letter_hooks
    for h in letter_hooks:
        try:
            keyboard.unhook(h)
        except Exception:
            pass
    letter_hooks.clear()

def on_right_alt_down(event):
    global right_alt_held
    if not right_alt_held:
        right_alt_held = True
        show_native_window()
        enable_letter_hooks()

def on_right_alt_up(event):
    global right_alt_held
    if right_alt_held:
        right_alt_held = False
        hide_native_window()
        disable_letter_hooks()


# --- WINDOW MANAGEMENT ---

def get_window_text(hwnd):
    length = user32.GetWindowTextLengthW(hwnd)
    if length == 0:
        return ""
    buffer = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buffer, length + 1)
    return buffer.value.strip()


def is_alt_tab_window(hwnd):
    if not user32.IsWindowVisible(hwnd):
        return False

    title = get_window_text(hwnd)
    if not title:
        return False

    if title == VIEWPORT_TITLE:
        return False

    exstyle = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    if exstyle & WS_EX_TOOLWINDOW:
        return False

    root_owner = user32.GetAncestor(hwnd, GA_ROOTOWNER)
    if root_owner != hwnd:
        return False

    return True


def get_process_name_from_hwnd(hwnd):
    pid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if not pid.value:
        return ""

    process_handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
    if not process_handle:
        return ""

    try:
        buffer_len = ctypes.c_ulong(260)
        buffer = ctypes.create_unicode_buffer(buffer_len.value)
        success = kernel32.QueryFullProcessImageNameW(process_handle, 0, buffer, ctypes.byref(buffer_len))
        if not success:
            return ""
        return os.path.basename(buffer.value)
    finally:
        kernel32.CloseHandle(process_handle)


def activate_window(hwnd):
    """Bypasses Windows Focus Stealing Prevention to force windows to the front."""
    SWP_NOMOVE = 0x0002
    SWP_NOSIZE = 0x0001
    SWP_SHOWWINDOW = 0x0040
    HWND_TOPMOST = -1
    HWND_NOTOPMOST = -2

    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, SW_RESTORE)

    user32.SwitchToThisWindow(hwnd, True)

    foreground_hwnd = user32.GetForegroundWindow()
    if foreground_hwnd != hwnd:
        foreground_thread = user32.GetWindowThreadProcessId(foreground_hwnd, None)
        current_thread = kernel32.GetCurrentThreadId()

        if foreground_thread != 0 and foreground_thread != current_thread:
            user32.AttachThreadInput(current_thread, foreground_thread, True)
            
            user32.SetForegroundWindow(hwnd)
            user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
            user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
            user32.BringWindowToTop(hwnd)
            
            user32.AttachThreadInput(current_thread, foreground_thread, False)
        else:
            user32.SetForegroundWindow(hwnd)
            user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
            user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
            user32.BringWindowToTop(hwnd)


def select_app_by_letter(letter):
    global current_letter_indices

    letter = letter.lower()
    if letter not in current_letter_groups:
        return

    group = current_letter_groups[letter]
    if not group:
        return

    index = current_letter_indices.get(letter, 0) % len(group)
    entry = group[index]

    current_letter_indices[letter] = (index + 1) % len(group)
    update_ui()
    
    activate_window(entry["hwnd"])


def exe_to_app_name(exe_name, window_title=""):
    exe = exe_name.lower()
    overrides = config.get("app_overrides", {})

    if exe == "applicationframehost.exe":
        title_lower = window_title.lower()
        if "settings" in title_lower:
            return "Settings"
        if window_title:
            return window_title
        return "Windows App"

    if exe == "chrome.exe" and "chatgpt" in window_title.lower():
        return "ChatGPT"

    if exe in overrides:
        return overrides[exe]

    base = os.path.splitext(exe_name)[0]
    if base.endswith("64"):
        base = base[:-2]

    if not base:
        return ""

    return base[:1].upper() + base[1:]


def get_alt_tab_app_entries():
    entries = []
    enum_proc_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def enum_proc(hwnd, lparam):
        if is_alt_tab_window(hwnd):
            title = get_window_text(hwnd)
            exe_name = get_process_name_from_hwnd(hwnd)

            if exe_name:
                app_name = exe_to_app_name(exe_name, title)
                if app_name:
                    
                    custom_letter = config.get("letter_overrides", {}).get(app_name)
                    if custom_letter and len(custom_letter) == 1:
                        letter = custom_letter.lower()
                    else:
                        letter = app_name[0].lower()

                    entries.append({
                        "hwnd": hwnd,
                        "title": title,
                        "exe": exe_name,
                        "app_name": app_name,
                        "letter": letter,
                    })
        return True

    user32.EnumWindows(enum_proc_type(enum_proc), 0)
    return entries


def keyboard_thread():
    keyboard.on_press_key("right alt", on_right_alt_down, suppress=True)
    keyboard.on_release_key("right alt", on_right_alt_up, suppress=True)
    
    try:
        keyboard.on_press_key("alt gr", on_right_alt_down, suppress=True)
        keyboard.on_release_key("alt gr", on_right_alt_up, suppress=True)
    except ValueError:
        pass

    keyboard.wait()


def make_tray_image():
    size = 64
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((8, 8, 56, 56), radius=12, fill=(35, 35, 35, 255))
    draw.text((22, 18), "R", fill=(255, 255, 255, 255))
    return image


def on_open_config(icon, item):
    """Opens the config file in the default text editor."""
    if not os.path.exists(CONFIG_FILE):
        save_config()
    try:
        os.startfile(CONFIG_FILE)
    except Exception as e:
        print(f"Error opening config: {e}")


def on_tray_quit(icon, item):
    dpg.stop_dearpygui()
    icon.stop()


def setup_tray():
    global tray_icon
    tray_icon = pystray.Icon(
        "ralt",
        icon=make_tray_image(),
        title="ralt",
        menu=pystray.Menu(
            pystray.MenuItem("Edit Config", on_open_config),
            pystray.MenuItem("Quit", on_tray_quit)
        ),
    )
    tray_icon.run_detached()


def main():
    global viewport_hwnd

    load_config()

    dpg.create_context()

    # --- FONT REGISTRY FOR LARGER TEXT ---
    with dpg.font_registry():
        font_path = "C:\\Windows\\Fonts\\segoeui.ttf"
        if os.path.exists(font_path):
            default_font = dpg.add_font(font_path, 18) 
            dpg.bind_font(default_font)

    with dpg.window(tag=WINDOW_TAG):
        
        # WE WRAP EVERYTHING IN A MASTER GROUP
        with dpg.group(tag="master_group"):
            dpg.add_text("rAlt")  # Changed text here
            dpg.add_separator()
            dpg.add_group(tag="app_list_group")

    dpg.create_viewport(
        title=VIEWPORT_TITLE,
        width=VIEWPORT_WIDTH,
        height=400, 
        decorated=False,
        resizable=False,
        always_on_top=True 
    )

    dpg.setup_dearpygui()
    dpg.show_viewport()
    
    dpg.set_primary_window(WINDOW_TAG, True)
    
    dpg.render_dearpygui_frame()

    viewport_hwnd = get_hwnd(VIEWPORT_TITLE)
    if viewport_hwnd:
        apply_toolwindow_style(viewport_hwnd)

    hide_native_window()
    setup_tray()

    t = threading.Thread(target=keyboard_thread, daemon=True)
    t.start()

    while dpg.is_dearpygui_running():
        dpg.render_dearpygui_frame()

    if tray_icon is not None:
        tray_icon.stop()

    dpg.destroy_context()


if __name__ == "__main__":
    main()