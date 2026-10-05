#pragma once

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


class RAltEngine final
{
public:
    RAltEngine() = default;
    ~RAltEngine();

    const char* Name() const { return "rAlt"; }

    bool IsOpen() const { return settingsOpen_; }
    void SetOpen(bool open) { settingsOpen_ = open; }

    void Initialize(HINSTANCE instance, HWND hostWindow);
    void Shutdown();
    bool Enabled() const { return enabled_; }
    void SetEnabled(bool enabled);
    void ReloadConfig() { LoadConfig(); }
    void OpenConfig();
    std::string ConfigPathUtf8() const { return WideToUtf8(configPath_.wstring()); }

private:
    struct WindowEntry
    {
        HWND hwnd{};
        std::wstring title;
        std::wstring exe;
        std::wstring appName;
        wchar_t letter{};
    };

    struct Config
    {
        std::map<std::wstring, std::wstring> appOverrides;
        std::map<std::wstring, std::wstring> letterOverrides;
    };

    static inline RAltEngine* instance_ = nullptr;

    HINSTANCE appInstance_ = nullptr;
    HWND hostWindow_ = nullptr;
    HWND overlayWindow_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;

    HFONT titleFont_ = nullptr;
    HFONT rowFont_ = nullptr;

    std::filesystem::path configPath_;
    Config config_;

    bool settingsOpen_ = false;
    bool enabled_ = true;
    bool triggerHeld_ = false;
    bool suppressEscUp_ = false;
    bool previousShortcutDown_ = false;
    bool settingsPositioned_ = false;

    HWND currentForeground_ = nullptr;
    HWND previousForeground_ = nullptr;

    std::map<wchar_t, std::vector<WindowEntry>> groups_;
    std::map<wchar_t, size_t> indices_;
    std::vector<HWND> recentHwnds_;
    std::vector<WindowEntry> enumerated_;

    static constexpr int kViewportWidth = 360;

    static LRESULT CALLBACK OverlayWndProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    static LRESULT CALLBACK KeyboardHookProc(
        int code,
        WPARAM wParam,
        LPARAM lParam);

    static void CALLBACK ForegroundWinEventProc(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD eventThread,
        DWORD eventTime);

    bool CreateOverlayWindow();
    void DestroyOverlayWindow();

    void TriggerDown();
    void TriggerUp();
    void Select(wchar_t letter);
    void SwitchToPreviousWindow();
    void TrackForegroundWindow(HWND hwnd);

    void Refresh();
    void ConsiderWindow(HWND hwnd);

    static BOOL CALLBACK EnumWindowsProc(
        HWND hwnd,
        LPARAM lParam);

    bool IsSwitchable(HWND hwnd) const;
    static std::wstring WindowText(HWND hwnd);
    static std::wstring ProcessName(HWND hwnd);

    std::wstring AppName(
        const std::wstring& exeName,
        const std::wstring& title) const;

    int DesiredOverlayHeight() const;
    void CenterOverlay(int desiredHeight);
    void ShowOverlay();
    void HideOverlay();
    void PaintOverlay();

    static void Activate(HWND hwnd);

    void LoadConfig();
    void SaveConfig() const;

    static Config DefaultConfig();

    static std::filesystem::path ExecutableDirectory();

    static std::wstring Utf8ToWide(const std::string& text);
    static std::string WideToUtf8(const std::wstring& text);
    static std::wstring ToLower(std::wstring value);

    static int HexDigit(char ch);
    static unsigned int ParseHex4(
        std::string_view text,
        size_t& pos);

    static void AppendUtf8Codepoint(
        std::string& out,
        unsigned int cp);

    static void SkipWhitespace(
        std::string_view text,
        size_t& pos);

    static std::string ParseJsonString(
        std::string_view text,
        size_t& pos);

    static std::optional<std::string_view> FindObjectBody(
        std::string_view json,
        std::string_view key);

    static std::map<std::wstring, std::wstring> ParseStringMap(
        std::string_view objectBody);

    static std::string JsonEscape(
        const std::string& value);

    static void WriteStringMap(
        std::ofstream& out,
        const std::map<std::wstring, std::wstring>& values,
        int indent);
};
