#include "ralt/RAltEngine.h"
#include <shellapi.h>
#include <cwchar>

namespace {
RAltEngine engine;
constexpr UINT kTrayMessage = WM_APP + 40;
constexpr UINT kEnable = 100, kEdit = 101, kReload = 102, kExit = 103;
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;
void AddTray(HWND window) {
    tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = kTrayMessage;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, _countof(tray.szTip), L"rAlt");
    Shell_NotifyIconW(NIM_ADD, &tray);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbarCreated && message == taskbarCreated) { AddTray(window); return 0; }
    if (message == kTrayMessage && (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (engine.Enabled() ? MF_CHECKED : 0), kEnable, L"Enabled");
        AppendMenuW(menu, MF_STRING, kEdit, L"Edit config");
        AppendMenuW(menu, MF_STRING, kReload, L"Reload config");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kExit, L"Exit");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
        switch (choice) {
            case kEnable: engine.SetEnabled(!engine.Enabled()); break;
            case kEdit: engine.OpenConfig(); break;
            case kReload: engine.ReloadConfig(); break;
            case kExit: DestroyWindow(window); break;
        }
        return 0;
    }
    if (message == WM_DESTROY) { Shell_NotifyIconW(NIM_DELETE, &tray); engine.Shutdown(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\rAltStandalone");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 0; }
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = WindowProc; cls.lpszClassName = L"rAltTrayHost";
    if (!RegisterClassExW(&cls)) { CloseHandle(mutex); return 1; }
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"rAlt", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(mutex); return 1; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    engine.Initialize(instance, window); AddTray(window);
    MSG message{}; int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (result < 0) { Shell_NotifyIconW(NIM_DELETE, &tray); engine.Shutdown(); }
    CloseHandle(mutex); return result < 0 ? 1 : 0;
}
