#include "ralt/RAltEngine.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <stdexcept>
#include <string_view>

#include <shellapi.h>


namespace
{
    constexpr wchar_t kOverlayClassName[] =
        L"DashboardRAltOverlayWindow";

    constexpr wchar_t kOverlayTitle[] =
        L"ralt_overlay";
}

RAltEngine::~RAltEngine()
{
    Shutdown();
}

void RAltEngine::Initialize(HINSTANCE instance, HWND hostWindow)
{
    appInstance_ = instance;
    hostWindow_ = hostWindow;

    configPath_ =
        ExecutableDirectory() /
        L"ralt_config.json";

    LoadConfig();

    instance_ = this;

    if (!CreateOverlayWindow())
    {
        enabled_ = false;
        return;
    }

    keyboardHook_ =
        SetWindowsHookExW(
            WH_KEYBOARD_LL,
            KeyboardHookProc,
            appInstance_,
            0);

    if (!keyboardHook_)
        enabled_ = false;
}

void RAltEngine::Shutdown()
{
    if (keyboardHook_)
    {
        UnhookWindowsHookEx(
            keyboardHook_);

        keyboardHook_ = nullptr;
    }

    HideOverlay();
    DestroyOverlayWindow();

    if (instance_ == this)
        instance_ = nullptr;

    appInstance_ = nullptr;
    hostWindow_ = nullptr;
}

void RAltEngine::SetEnabled(bool enabled)
{
    enabled_ = enabled;

    if (!enabled_)
    {
        triggerHeld_ = false;
        suppressEscUp_ = false;
        HideOverlay();
    }
}

bool RAltEngine::CreateOverlayWindow()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = appInstance_;
    wc.lpfnWndProc = OverlayWndProc;
    wc.lpszClassName =
        kOverlayClassName;

    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);

    if (!RegisterClassExW(&wc) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    overlayWindow_ =
        CreateWindowExW(
            WS_EX_TOOLWINDOW |
                WS_EX_TOPMOST |
                WS_EX_NOACTIVATE,
            kOverlayClassName,
            kOverlayTitle,
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            kViewportWidth,
            200,
            nullptr,
            nullptr,
            appInstance_,
            this);

    if (!overlayWindow_)
        return false;

    titleFont_ =
        CreateFontW(
            -17,
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH |
                FF_DONTCARE,
            L"Segoe UI");

    rowFont_ =
        CreateFontW(
            -17,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH |
                FF_DONTCARE,
            L"Segoe UI");

    return true;
}

void RAltEngine::DestroyOverlayWindow()
{
    if (overlayWindow_)
    {
        DestroyWindow(
            overlayWindow_);

        overlayWindow_ = nullptr;
    }

    if (titleFont_)
    {
        DeleteObject(titleFont_);
        titleFont_ = nullptr;
    }

    if (rowFont_)
    {
        DeleteObject(rowFont_);
        rowFont_ = nullptr;
    }

    if (appInstance_)
    {
        UnregisterClassW(
            kOverlayClassName,
            appInstance_);
    }
}

LRESULT CALLBACK
RAltEngine::OverlayWndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    RAltEngine* self =
        reinterpret_cast<RAltEngine*>(
            GetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create =
            reinterpret_cast<
                const CREATESTRUCTW*>(
                    lParam);

        self =
            static_cast<RAltEngine*>(
                create->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                self));
    }

    if (!self)
    {
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    switch (message)
    {
    case WM_PAINT:
        self->PaintOverlay();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }
}

LRESULT CALLBACK
RAltEngine::KeyboardHookProc(
    int code,
    WPARAM wParam,
    LPARAM lParam)
{
    if (code != HC_ACTION ||
        !instance_ ||
        !instance_->enabled_)
    {
        return CallNextHookEx(
            nullptr,
            code,
            wParam,
            lParam);
    }

    const auto* key =
        reinterpret_cast<
            const KBDLLHOOKSTRUCT*>(
                lParam);

    const bool down =
        wParam == WM_KEYDOWN ||
        wParam == WM_SYSKEYDOWN;

    const bool up =
        wParam == WM_KEYUP ||
        wParam == WM_SYSKEYUP;

    const bool rightAlt =
        key->vkCode == VK_RMENU ||
        (key->vkCode == VK_MENU &&
         (key->flags &
          LLKHF_EXTENDED) != 0);

    if (rightAlt)
    {
        if (down)
            instance_->TriggerDown();
        else if (up)
            instance_->TriggerUp();

        return CallNextHookEx(
            nullptr,
            code,
            wParam,
            lParam);
    }

    if (instance_->triggerHeld_)
    {
        if (key->vkCode >= 'A' &&
            key->vkCode <= 'Z')
        {
            if (down)
            {
                const wchar_t letter =
                    static_cast<wchar_t>(
                        L'a' +
                        (key->vkCode - 'A'));

                instance_->Select(letter);
            }

            return 1;
        }

        if (key->vkCode == VK_ESCAPE)
        {
            if (down)
            {
                instance_->suppressEscUp_ = true;
                instance_->TriggerUp();
            }

            return 1;
        }
    }
    else if (
        instance_->suppressEscUp_ &&
        key->vkCode == VK_ESCAPE &&
        up)
    {
        instance_->suppressEscUp_ = false;
        return 1;
    }

    return CallNextHookEx(
        nullptr,
        code,
        wParam,
        lParam);
}

void RAltEngine::TriggerDown()
{
    if (!enabled_ || triggerHeld_)
        return;

    triggerHeld_ = true;
    ShowOverlay();
}

void RAltEngine::TriggerUp()
{
    if (!triggerHeld_)
        return;

    triggerHeld_ = false;
    HideOverlay();
}

void RAltEngine::Select(
    wchar_t letter)
{
    const auto groupIt =
        groups_.find(letter);

    if (groupIt == groups_.end() ||
        groupIt->second.empty())
    {
        return;
    }

    auto& group = groupIt->second;
    size_t& index = indices_[letter];

    index %= group.size();

    const WindowEntry entry =
        group[index];

    index =
        (index + 1) %
        group.size();

    recentHwnds_.erase(
        std::remove(
            recentHwnds_.begin(),
            recentHwnds_.end(),
            entry.hwnd),
        recentHwnds_.end());

    recentHwnds_.insert(
        recentHwnds_.begin(),
        entry.hwnd);

    if (recentHwnds_.size() > 100)
        recentHwnds_.resize(100);

    HideOverlay();
    Activate(entry.hwnd);
}

void RAltEngine::Refresh()
{
    enumerated_.clear();
    groups_.clear();

    EnumWindows(
        EnumWindowsProc,
        reinterpret_cast<LPARAM>(
            this));

    std::map<HWND, size_t> recentRank;

    for (size_t i = 0;
         i < recentHwnds_.size();
         ++i)
    {
        recentRank[
            recentHwnds_[i]] = i;
    }

    std::sort(
        enumerated_.begin(),
        enumerated_.end(),
        [&recentRank](
            const WindowEntry& a,
            const WindowEntry& b)
        {
            const size_t aRank =
                recentRank.contains(a.hwnd)
                    ? recentRank.at(a.hwnd)
                    : 10000;

            const size_t bRank =
                recentRank.contains(b.hwnd)
                    ? recentRank.at(b.hwnd)
                    : 10000;

            if (a.letter != b.letter)
                return a.letter < b.letter;

            if (aRank != bRank)
                return aRank < bRank;

            return ToLower(a.appName) <
                ToLower(b.appName);
        });

    for (const auto& entry : enumerated_)
        groups_[entry.letter].push_back(entry);

    indices_.clear();

    for (const auto& [letter, entries] :
         groups_)
    {
        (void)entries;
        indices_[letter] = 0;
    }
}

BOOL CALLBACK
RAltEngine::EnumWindowsProc(
    HWND hwnd,
    LPARAM lParam)
{
    auto* self =
        reinterpret_cast<RAltEngine*>(
            lParam);

    self->ConsiderWindow(hwnd);
    return TRUE;
}

void RAltEngine::ConsiderWindow(
    HWND hwnd)
{
    if (!IsSwitchable(hwnd))
        return;

    const std::wstring title =
        WindowText(hwnd);

    const std::wstring exe =
        ProcessName(hwnd);

    if (exe.empty())
        return;

    const std::wstring name =
        AppName(exe, title);

    std::wstring custom;

    if (const auto it =
            config_.letterOverrides.find(
                name);
        it !=
            config_.letterOverrides.end())
    {
        custom = ToLower(it->second);
    }

    wchar_t letter = 0;

    if (custom.size() == 1)
    {
        letter = custom[0];
    }
    else if (!name.empty())
    {
        letter =
            static_cast<wchar_t>(
                std::towlower(
                    name.front()));
    }

    if (letter < L'a' ||
        letter > L'z')
    {
        return;
    }

    enumerated_.push_back(
        WindowEntry{
            hwnd,
            title,
            exe,
            name,
            letter
        });
}

bool RAltEngine::IsSwitchable(
    HWND hwnd) const
{
    if (!IsWindowVisible(hwnd))
        return false;

    if (hwnd == overlayWindow_ ||
        hwnd == hostWindow_)
    {
        return false;
    }

    const std::wstring title =
        WindowText(hwnd);

    if (title.empty() ||
        title == kOverlayTitle)
    {
        return false;
    }

    const LONG_PTR style =
        GetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE);

    if ((style &
         WS_EX_TOOLWINDOW) != 0)
    {
        return false;
    }

    return GetAncestor(
        hwnd,
        GA_ROOTOWNER) == hwnd;
}

std::wstring RAltEngine::WindowText(
    HWND hwnd)
{
    const int length =
        GetWindowTextLengthW(hwnd);

    if (length <= 0)
        return {};

    std::wstring text(
        static_cast<size_t>(
            length + 1),
        L'\0');

    const int copied =
        GetWindowTextW(
            hwnd,
            text.data(),
            static_cast<int>(
                text.size()));

    if (copied <= 0)
        return {};

    text.resize(
        static_cast<size_t>(
            copied));

    while (!text.empty() &&
           iswspace(text.front()))
    {
        text.erase(text.begin());
    }

    while (!text.empty() &&
           iswspace(text.back()))
    {
        text.pop_back();
    }

    return text;
}

std::wstring RAltEngine::ProcessName(
    HWND hwnd)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(
        hwnd,
        &pid);

    if (pid == 0)
        return {};

    HANDLE process =
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            pid);

    if (!process)
        return {};

    std::wstring buffer(
        32768,
        L'\0');

    DWORD size =
        static_cast<DWORD>(
            buffer.size());

    std::wstring result;

    if (QueryFullProcessImageNameW(
            process,
            0,
            buffer.data(),
            &size))
    {
        buffer.resize(size);

        result =
            std::filesystem::path(
                buffer)
                .filename()
                .wstring();
    }

    CloseHandle(process);
    return result;
}

std::wstring RAltEngine::AppName(
    const std::wstring& exeName,
    const std::wstring& title) const
{
    const std::wstring exe =
        ToLower(exeName);

    const std::wstring lowerTitle =
        ToLower(title);

    if (exe ==
        L"applicationframehost.exe")
    {
        return lowerTitle.find(
                   L"settings") !=
                   std::wstring::npos
            ? L"Settings"
            : (title.empty()
                   ? L"Windows App"
                   : title);
    }

    if (exe == L"chrome.exe" &&
        lowerTitle.find(L"chatgpt") !=
            std::wstring::npos)
    {
        return L"ChatGPT";
    }

    if (const auto it =
            config_.appOverrides.find(
                exe);
        it !=
            config_.appOverrides.end())
    {
        return it->second;
    }

    std::wstring name =
        std::filesystem::path(
            exeName)
            .stem()
            .wstring();

    if (name.size() >= 2 &&
        name.ends_with(L"64"))
    {
        name.resize(
            name.size() - 2);
    }

    if (!name.empty())
    {
        name[0] =
            static_cast<wchar_t>(
                std::towupper(
                    name[0]));
    }

    return name;
}

int RAltEngine::DesiredOverlayHeight() const
{
    const int rowCount =
        groups_.empty()
            ? 1
            : static_cast<int>(
                groups_.size());

    return 62 +
        rowCount * 24 +
        12;
}

void RAltEngine::CenterOverlay(
    int desiredHeight)
{
    if (!overlayWindow_)
        return;

    POINT cursor{};
    GetCursorPos(&cursor);

    HMONITOR monitor =
        MonitorFromPoint(
            cursor,
            MONITOR_DEFAULTTONEAREST);

    MONITORINFO info{};
    info.cbSize = sizeof(info);

    if (!GetMonitorInfoW(
            monitor,
            &info))
    {
        return;
    }

    const RECT area =
        info.rcWork;

    const int workWidth =
        area.right -
        area.left;

    const int workHeight =
        area.bottom -
        area.top;

    const int height =
        std::min(
            desiredHeight,
            workHeight * 85 / 100);

    const int x =
        area.left +
        (workWidth -
         kViewportWidth) / 2;

    const int y =
        area.top +
        (workHeight -
         height) / 2;

    SetWindowPos(
        overlayWindow_,
        HWND_TOPMOST,
        x,
        y,
        kViewportWidth,
        height,
        SWP_NOACTIVATE |
            SWP_SHOWWINDOW);
}

void RAltEngine::ShowOverlay()
{
    if (!enabled_ ||
        !overlayWindow_)
    {
        return;
    }

    Refresh();
    CenterOverlay(
        DesiredOverlayHeight());

    InvalidateRect(
        overlayWindow_,
        nullptr,
        TRUE);

    UpdateWindow(
        overlayWindow_);
}

void RAltEngine::HideOverlay()
{
    if (overlayWindow_)
    {
        ShowWindow(
            overlayWindow_,
            SW_HIDE);
    }
}

void RAltEngine::PaintOverlay()
{
    PAINTSTRUCT ps{};

    HDC dc =
        BeginPaint(
            overlayWindow_,
            &ps);

    RECT client{};
    GetClientRect(
        overlayWindow_,
        &client);

    HBRUSH background =
        CreateSolidBrush(
            RGB(38, 38, 38));

    FillRect(
        dc,
        &client,
        background);

    DeleteObject(background);

    SetBkMode(
        dc,
        TRANSPARENT);

    RECT titleRect{
        14,
        10,
        client.right - 14,
        36
    };

    SelectObject(
        dc,
        titleFont_);

    SetTextColor(
        dc,
        RGB(140, 180, 255));

    DrawTextW(
        dc,
        L"rAlt",
        -1,
        &titleRect,
        DT_LEFT |
            DT_SINGLELINE |
            DT_VCENTER);

    HPEN separator =
        CreatePen(
            PS_SOLID,
            1,
            RGB(72, 72, 72));

    HPEN oldPen =
        static_cast<HPEN>(
            SelectObject(
                dc,
                separator));

    MoveToEx(
        dc,
        14,
        42,
        nullptr);

    LineTo(
        dc,
        client.right - 14,
        42);

    SelectObject(
        dc,
        oldPen);

    DeleteObject(separator);

    SelectObject(
        dc,
        rowFont_);

    SetTextColor(
        dc,
        RGB(235, 235, 235));

    int y = 52;

    if (groups_.empty())
    {
        SetTextColor(
            dc,
            RGB(150, 150, 150));

        TextOutW(
            dc,
            14,
            y,
            L"No windows",
            10);
    }
    else
    {
        for (const auto&
                 [letter, entries] :
             groups_)
        {
            std::vector<std::wstring>
                names;

            for (const auto& entry :
                 entries)
            {
                if (std::find(
                        names.begin(),
                        names.end(),
                        entry.appName) ==
                    names.end())
                {
                    names.push_back(
                        entry.appName);
                }
            }

            std::wstring line;

            line.push_back(
                static_cast<wchar_t>(
                    std::towupper(
                        letter)));

            line += L"   ";

            for (size_t i = 0;
                 i < names.size();
                 ++i)
            {
                if (i != 0)
                    line += L" / ";

                line += names[i];
            }

            RECT rowRect{
                14,
                y,
                client.right - 14,
                y + 22
            };

            DrawTextW(
                dc,
                line.c_str(),
                static_cast<int>(
                    line.size()),
                &rowRect,
                DT_LEFT |
                    DT_SINGLELINE |
                    DT_VCENTER |
                    DT_END_ELLIPSIS);

            y += 24;
        }
    }

    EndPaint(
        overlayWindow_,
        &ps);
}

void RAltEngine::Activate(
    HWND hwnd)
{
    if (!IsWindow(hwnd))
        return;

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    HWND foreground =
        GetForegroundWindow();

    const DWORD foregroundThread =
        foreground
            ? GetWindowThreadProcessId(
                  foreground,
                  nullptr)
            : 0;

    const DWORD currentThread =
        GetCurrentThreadId();

    const bool attached =
        foregroundThread != 0 &&
        foregroundThread !=
            currentThread;

    if (attached)
    {
        AttachThreadInput(
            currentThread,
            foregroundThread,
            TRUE);
    }

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);

    if (attached)
    {
        AttachThreadInput(
            currentThread,
            foregroundThread,
            FALSE);
    }
}

RAltEngine::Config
RAltEngine::DefaultConfig()
{
    Config config;

    config.appOverrides = {
        {L"msedge.exe", L"Edge"},
        {L"chrome.exe", L"Chrome"},
        {L"rider64.exe", L"Rider"},
        {L"code.exe", L"VS Code"},
        {L"devenv.exe", L"Visual Studio"},
        {L"explorer.exe", L"Explorer"},
        {L"notepad.exe", L"Notepad"},
        {L"cmd.exe", L"Command Prompt"},
        {L"powershell.exe", L"PowerShell"},
        {L"windowsterminal.exe", L"Terminal"},
        {L"wt.exe", L"Terminal"},
    };

    return config;
}

void RAltEngine::LoadConfig()
{
    Config loaded =
        DefaultConfig();

    std::ifstream input(
        configPath_,
        std::ios::binary);

    if (!input)
    {
        config_ =
            std::move(loaded);

        SaveConfig();
        return;
    }

    try
    {
        std::string json(
            (std::istreambuf_iterator<char>(
                input)),
            std::istreambuf_iterator<char>());

        if (auto body =
                FindObjectBody(
                    json,
                    "app_overrides"))
        {
            for (auto&
                     [key, value] :
                 ParseStringMap(*body))
            {
                loaded.appOverrides[
                    key] = value;
            }
        }

        if (auto body =
                FindObjectBody(
                    json,
                    "letter_overrides"))
        {
            for (auto&
                     [key, value] :
                 ParseStringMap(*body))
            {
                loaded.letterOverrides[
                    key] = value;
            }
        }

        config_ =
            std::move(loaded);
    }
    catch (...)
    {
        config_ =
            std::move(loaded);
    }
}

void RAltEngine::SaveConfig() const
{
    std::ofstream out(
        configPath_,
        std::ios::binary |
            std::ios::trunc);

    if (!out)
        return;

    out << "{\n";
    out << "    \"app_overrides\": ";

    WriteStringMap(
        out,
        config_.appOverrides,
        8);

    out << ",\n";
    out << "    \"letter_overrides\": ";

    WriteStringMap(
        out,
        config_.letterOverrides,
        8);

    out << "\n}\n";
}

void RAltEngine::OpenConfig()
{
    if (!std::filesystem::exists(
            configPath_))
    {
        SaveConfig();
    }

    ShellExecuteW(
        hostWindow_,
        L"open",
        configPath_.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
}

std::filesystem::path
RAltEngine::ExecutableDirectory()
{
    std::wstring buffer(
        32768,
        L'\0');

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(
                buffer.size()));

    if (length == 0 ||
        length >= buffer.size())
    {
        return std::filesystem::
            current_path();
    }

    buffer.resize(length);

    return std::filesystem::path(
               buffer)
        .parent_path();
}

std::wstring RAltEngine::Utf8ToWide(
    const std::string& text)
{
    if (text.empty())
        return {};

    int count =
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(
                text.size()),
            nullptr,
            0);

    if (count <= 0)
    {
        count =
            MultiByteToWideChar(
                CP_UTF8,
                0,
                text.data(),
                static_cast<int>(
                    text.size()),
                nullptr,
                0);
    }

    if (count <= 0)
        return {};

    std::wstring result(
        static_cast<size_t>(
            count),
        L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(
            text.size()),
        result.data(),
        count);

    return result;
}

std::string RAltEngine::WideToUtf8(
    const std::wstring& text)
{
    if (text.empty())
        return {};

    const int count =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(
                text.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

    if (count <= 0)
        return {};

    std::string result(
        static_cast<size_t>(
            count),
        '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(
            text.size()),
        result.data(),
        count,
        nullptr,
        nullptr);

    return result;
}

std::wstring RAltEngine::ToLower(
    std::wstring value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t ch)
        {
            return static_cast<wchar_t>(
                std::towlower(ch));
        });

    return value;
}

void RAltEngine::AppendUtf8Codepoint(
    std::string& out,
    unsigned int cp)
{
    if (cp <= 0x7F)
    {
        out.push_back(
            static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        out.push_back(
            static_cast<char>(
                0xC0 | (cp >> 6)));

        out.push_back(
            static_cast<char>(
                0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(
            static_cast<char>(
                0xE0 | (cp >> 12)));

        out.push_back(
            static_cast<char>(
                0x80 |
                ((cp >> 6) & 0x3F)));

        out.push_back(
            static_cast<char>(
                0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(
            static_cast<char>(
                0xF0 | (cp >> 18)));

        out.push_back(
            static_cast<char>(
                0x80 |
                ((cp >> 12) & 0x3F)));

        out.push_back(
            static_cast<char>(
                0x80 |
                ((cp >> 6) & 0x3F)));

        out.push_back(
            static_cast<char>(
                0x80 | (cp & 0x3F)));
    }
}

int RAltEngine::HexDigit(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';

    if (ch >= 'a' && ch <= 'f')
        return 10 + ch - 'a';

    if (ch >= 'A' && ch <= 'F')
        return 10 + ch - 'A';

    return -1;
}

unsigned int RAltEngine::ParseHex4(
    std::string_view text,
    size_t& pos)
{
    if (pos + 4 > text.size())
    {
        throw std::runtime_error(
            "Incomplete JSON unicode escape");
    }

    unsigned int value = 0;

    for (int i = 0; i < 4; ++i)
    {
        const int digit =
            HexDigit(text[pos++]);

        if (digit < 0)
        {
            throw std::runtime_error(
                "Invalid JSON unicode escape");
        }

        value =
            (value << 4) |
            static_cast<unsigned int>(
                digit);
    }

    return value;
}

void RAltEngine::SkipWhitespace(
    std::string_view text,
    size_t& pos)
{
    while (pos < text.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   text[pos])))
    {
        ++pos;
    }
}

std::string RAltEngine::ParseJsonString(
    std::string_view text,
    size_t& pos)
{
    SkipWhitespace(text, pos);

    if (pos >= text.size() ||
        text[pos] != '"')
    {
        throw std::runtime_error(
            "Expected JSON string");
    }

    ++pos;

    std::string out;

    while (pos < text.size())
    {
        const char ch =
            text[pos++];

        if (ch == '"')
            return out;

        if (ch != '\\')
        {
            out.push_back(ch);
            continue;
        }

        if (pos >= text.size())
        {
            throw std::runtime_error(
                "Incomplete JSON escape");
        }

        const char esc =
            text[pos++];

        switch (esc)
        {
        case '"':
            out.push_back('"');
            break;

        case '\\':
            out.push_back('\\');
            break;

        case '/':
            out.push_back('/');
            break;

        case 'b':
            out.push_back('\b');
            break;

        case 'f':
            out.push_back('\f');
            break;

        case 'n':
            out.push_back('\n');
            break;

        case 'r':
            out.push_back('\r');
            break;

        case 't':
            out.push_back('\t');
            break;

        case 'u':
        {
            unsigned int cp =
                ParseHex4(text, pos);

            if (cp >= 0xD800 &&
                cp <= 0xDBFF &&
                pos + 6 <= text.size() &&
                text[pos] == '\\' &&
                text[pos + 1] == 'u')
            {
                size_t lowPos =
                    pos + 2;

                unsigned int low =
                    ParseHex4(
                        text,
                        lowPos);

                if (low >= 0xDC00 &&
                    low <= 0xDFFF)
                {
                    pos = lowPos;

                    cp =
                        0x10000 +
                        ((cp - 0xD800) << 10) +
                        (low - 0xDC00);
                }
            }

            AppendUtf8Codepoint(
                out,
                cp);

            break;
        }

        default:
            throw std::runtime_error(
                "Invalid JSON escape");
        }
    }

    throw std::runtime_error(
        "Unterminated JSON string");
}

std::optional<std::string_view>
RAltEngine::FindObjectBody(
    std::string_view json,
    std::string_view key)
{
    const std::string needle =
        "\"" +
        std::string(key) +
        "\"";

    const size_t keyPos =
        json.find(needle);

    if (keyPos ==
        std::string_view::npos)
    {
        return std::nullopt;
    }

    const size_t colon =
        json.find(
            ':',
            keyPos +
                needle.size());

    if (colon ==
        std::string_view::npos)
    {
        throw std::runtime_error(
            "Missing ':' after JSON key");
    }

    const size_t open =
        json.find(
            '{',
            colon + 1);

    if (open ==
        std::string_view::npos)
    {
        throw std::runtime_error(
            "Expected JSON object");
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = open;
         i < json.size();
         ++i)
    {
        const char ch =
            json[i];

        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                inString = false;

            continue;
        }

        if (ch == '"')
        {
            inString = true;
        }
        else if (ch == '{')
        {
            ++depth;
        }
        else if (ch == '}')
        {
            --depth;

            if (depth == 0)
            {
                return json.substr(
                    open + 1,
                    i - open - 1);
            }
        }
    }

    throw std::runtime_error(
        "Unterminated JSON object");
}

std::map<std::wstring, std::wstring>
RAltEngine::ParseStringMap(
    std::string_view objectBody)
{
    std::map<
        std::wstring,
        std::wstring> result;

    size_t pos = 0;

    while (true)
    {
        SkipWhitespace(
            objectBody,
            pos);

        if (pos >=
            objectBody.size())
        {
            break;
        }

        if (objectBody[pos] == ',')
        {
            ++pos;
            continue;
        }

        const std::string key =
            ParseJsonString(
                objectBody,
                pos);

        SkipWhitespace(
            objectBody,
            pos);

        if (pos >=
                objectBody.size() ||
            objectBody[pos] != ':')
        {
            throw std::runtime_error(
                "Expected ':' in JSON object");
        }

        ++pos;

        const std::string value =
            ParseJsonString(
                objectBody,
                pos);

        result[
            Utf8ToWide(key)] =
            Utf8ToWide(value);

        SkipWhitespace(
            objectBody,
            pos);

        if (pos <
            objectBody.size())
        {
            if (objectBody[pos] == ',')
            {
                ++pos;
            }
            else
            {
                throw std::runtime_error(
                    "Expected ',' in JSON object");
            }
        }
    }

    return result;
}

std::string RAltEngine::JsonEscape(
    const std::string& value)
{
    std::string out;
    out.reserve(
        value.size() + 8);

    constexpr char hex[] =
        "0123456789ABCDEF";

    for (unsigned char ch : value)
    {
        switch (ch)
        {
        case '"':
            out += "\\\"";
            break;

        case '\\':
            out += "\\\\";
            break;

        case '\b':
            out += "\\b";
            break;

        case '\f':
            out += "\\f";
            break;

        case '\n':
            out += "\\n";
            break;

        case '\r':
            out += "\\r";
            break;

        case '\t':
            out += "\\t";
            break;

        default:
            if (ch < 0x20)
            {
                out += "\\u00";

                out.push_back(
                    hex[
                        (ch >> 4) &
                        0x0F]);

                out.push_back(
                    hex[ch & 0x0F]);
            }
            else
            {
                out.push_back(
                    static_cast<char>(
                        ch));
            }

            break;
        }
    }

    return out;
}

void RAltEngine::WriteStringMap(
    std::ofstream& out,
    const std::map<
        std::wstring,
        std::wstring>& values,
    int indent)
{
    out << "{";

    if (!values.empty())
        out << "\n";

    size_t index = 0;

    for (const auto&
             [key, value] :
         values)
    {
        out
            << std::string(
                   static_cast<size_t>(
                       indent),
                   ' ')
            << "\""
            << JsonEscape(
                   WideToUtf8(key))
            << "\": \""
            << JsonEscape(
                   WideToUtf8(value))
            << "\"";

        if (++index <
            values.size())
        {
            out << ",";
        }

        out << "\n";
    }

    if (!values.empty())
    {
        out << std::string(
            static_cast<size_t>(
                indent - 4),
            ' ');
    }

    out << "}";
}
