
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <wincodec.h>
#include <magnification.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

static constexpr float DEFAULT_MAGNIFICATION = 3.0f;
static constexpr double HUNT_OFFSET_RATIO = 0.092592;
static constexpr UINT EXTENDED_SCAN_CODE_FLAG = 0x100;
static constexpr UINT MILLISECONDS_PER_SECOND = 1000u;

// ─── Config ──────────────────────────────────────────────────────────────────

struct Config
{
    DWORD toggleKey = 0x59; // Y
    DWORD magUpKey = VK_UP;
    DWORD magDownKey = VK_DOWN;
    float magStep = 1.0f;
    int frameRate = 60;
    int offsetY = 0;
    bool offsetIsHunt = true;
    int monitorIndex = 0;
    int windowWidth = 400;
    int windowHeight = 400;
};

static Config g_config;

static void GetIniPath(WCHAR *buf, DWORD len)
{
    GetModuleFileNameW(NULL, buf, len);
    PathRemoveFileSpecW(buf);
    PathAppendW(buf, L"settings.ini");
}

static int HuntOffset()
{
    return int(GetSystemMetrics(SM_CYSCREEN) * HUNT_OFFSET_RATIO);
}

static std::wstring KeyName(DWORD vk)
{
    UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_NUMLOCK)
        sc |= EXTENDED_SCAN_CODE_FLAG;
    WCHAR buf[64] = {};
    GetKeyNameTextW((LONG)(sc << 16), buf, 64);
    if (buf[0])
        return buf;
    WCHAR fallback[8];
    swprintf_s(fallback, L"0x%02X", vk);
    return fallback;
}

static void LoadConfig()
{
    WCHAR path[MAX_PATH];
    GetIniPath(path, MAX_PATH);

    auto getInt = [&](const WCHAR *key, int def)
    {
        return (int)GetPrivateProfileIntW(L"magnifier", key, def, path);
    };

    g_config.toggleKey = getInt(L"toggleKey", 0x59);
    g_config.magUpKey = getInt(L"magUpKey", VK_UP);
    g_config.magDownKey = getInt(L"magDownKey", VK_DOWN);
    g_config.monitorIndex = getInt(L"monitorIndex", 0);
    g_config.windowWidth = getInt(L"windowWidth", 400);
    g_config.windowHeight = getInt(L"windowHeight", 400);
    g_config.offsetIsHunt = getInt(L"offsetIsHunt", 1) != 0;

    g_config.frameRate = getInt(L"frameRate", 60);
    if (g_config.frameRate < 1)
        g_config.frameRate = 1;
    if (g_config.frameRate > 360)
        g_config.frameRate = 360;

    WCHAR stepText[32];
    GetPrivateProfileStringW(L"magnifier", L"magStep", L"1.0", stepText, 32, path);
    g_config.magStep = (float)_wtof(stepText);
    if (g_config.magStep < 0.1f)
        g_config.magStep = 0.1f;

    g_config.offsetY = g_config.offsetIsHunt
                           ? HuntOffset()
                           : getInt(L"offsetY", HuntOffset());
}

static void SaveConfig()
{
    WCHAR path[MAX_PATH];
    GetIniPath(path, MAX_PATH);

    auto writeInt = [&](const WCHAR *key, int val)
    {
        WCHAR text[32];
        swprintf_s(text, L"%d", val);
        WritePrivateProfileStringW(L"magnifier", key, text, path);
    };

    writeInt(L"toggleKey", (int)g_config.toggleKey);
    writeInt(L"magUpKey", (int)g_config.magUpKey);
    writeInt(L"magDownKey", (int)g_config.magDownKey);
    writeInt(L"monitorIndex", g_config.monitorIndex);
    writeInt(L"windowWidth", g_config.windowWidth);
    writeInt(L"windowHeight", g_config.windowHeight);
    writeInt(L"offsetIsHunt", g_config.offsetIsHunt ? 1 : 0);
    writeInt(L"offsetY", g_config.offsetY);
    writeInt(L"frameRate", g_config.frameRate);

    WCHAR stepText[32];
    swprintf_s(stepText, L"%.2f", g_config.magStep);
    WritePrivateProfileStringW(L"magnifier", L"magStep", stepText, path);
}

// ─── Monitor enumeration ─────────────────────────────────────────────────────

struct MonitorInfo
{
    HMONITOR handle;
    MONITORINFO mi;
};
static std::vector<MonitorInfo> g_monitors;

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM)
{
    MonitorInfo info;
    info.handle = hMon;
    info.mi.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoW(hMon, &info.mi);
    g_monitors.push_back(info);
    return TRUE;
}

static void EnumerateMonitors()
{
    g_monitors.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
}

static RECT GetMonitorRect(int idx)
{
    if (idx >= 0 && idx < (int)g_monitors.size())
        return g_monitors[idx].mi.rcMonitor;
    RECT r = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    return r;
}

// ─── Magnifier globals ───────────────────────────────────────────────────────

float g_magFactor = DEFAULT_MAGNIFICATION;
HINSTANCE hInst;
const WCHAR WindowClassName[] = L"MagnifierWindow";
HWND hwndMag;
HWND hwndHost;
RECT magWindowRect;
bool g_overlayVisible = false;

static UINT_PTR g_timerId = 0;

static UINT TimerInterval()
{
    UINT fps = (UINT)max(1, g_config.frameRate);
    UINT ms = MILLISECONDS_PER_SECOND / fps;
    return max(1u, ms);
}

// ─── Forward declarations ────────────────────────────────────────────────────

ATOM RegisterHostWindowClass(HINSTANCE);
BOOL SetupMagnifier(HINSTANCE);
LRESULT CALLBACK HostWndProc(HWND, UINT, WPARAM, LPARAM);
void CALLBACK UpdateMagWindow(HWND, UINT, UINT_PTR, DWORD);
LRESULT CALLBACK LowLevelKeyboardProc(int, WPARAM, LPARAM);
void OpenSettingsWindow();

// ─── Tray ────────────────────────────────────────────────────────────────────

#define WM_TRAY (WM_APP + 1)
#define ID_TRAY_SETTINGS 1001
#define ID_TRAY_EXIT 1002

static NOTIFYICONDATAW g_nid = {};

static void SetupTray(HWND hwnd)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Magnifier");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTray()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ─── Apply overlay position from current config ──────────────────────────────

static void ApplyOverlay()
{
    RECT mr = GetMonitorRect(g_config.monitorIndex);
    int cx = mr.left + (mr.right - mr.left) / 2;
    int cy = mr.top + (mr.bottom - mr.top) / 2;
    SetWindowPos(hwndHost, HWND_TOPMOST,
                 cx - g_config.windowWidth / 2,
                 cy + g_config.offsetY - g_config.windowHeight / 2,
                 g_config.windowWidth, g_config.windowHeight,
                 SWP_NOACTIVATE);
    GetClientRect(hwndHost, &magWindowRect);
    SetWindowPos(hwndMag, NULL,
                 magWindowRect.left, magWindowRect.top,
                 magWindowRect.right, magWindowRect.bottom, 0);
}

// ─── Settings window ─────────────────────────────────────────────────────────

static HWND g_hwndSettings = NULL;

#define ID_EDT_TOGGLE 2001
#define ID_EDT_MAGUP 2002
#define ID_EDT_MAGDOWN 2003
#define ID_EDT_STEP 2004
#define ID_EDT_WIDTH 2005
#define ID_EDT_HEIGHT 2006
#define ID_CMB_MONITOR 2007
#define ID_CMB_PRESET 2008
#define ID_EDT_OFFSET 2009
#define ID_BTN_CLOSE 2010
#define ID_BTN_EXIT 2011
#define ID_EDT_FPS 2012

struct KeyCapture
{
    HWND hwnd;
    DWORD vk;
    bool capturing;
    WNDPROC origProc;
    DWORD *configKey; // points directly into g_config
};
static KeyCapture g_keyCaptures[3];

static LRESULT CALLBACK KeyEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    KeyCapture *capture = nullptr;
    for (auto &k : g_keyCaptures)
        if (k.hwnd == hwnd)
        {
            capture = &k;
            break;
        }
    if (!capture)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (msg == WM_SETFOCUS)
    {
        capture->capturing = true;
        SetWindowTextW(hwnd, L"Press a key...");
        return 0;
    }
    if (msg == WM_KILLFOCUS)
    {
        capture->capturing = false;
        SetWindowTextW(hwnd, KeyName(capture->vk).c_str());
        return 0;
    }
    if (capture->capturing && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN))
    {
        DWORD vk = (DWORD)wParam;
        if (vk != VK_ESCAPE && vk != VK_RETURN && vk != VK_TAB)
        {
            capture->vk = vk;
            *capture->configKey = vk;
            SaveConfig();
        }
        SetWindowTextW(hwnd, KeyName(capture->vk).c_str());
        capture->capturing = false;
        SetFocus(GetParent(hwnd));
        return 0;
    }
    if (msg == WM_CHAR || msg == WM_PASTE)
        return 0;
    return CallWindowProcW(capture->origProc, hwnd, msg, wParam, lParam);
}

// Read all settings controls, apply live, and save.
// Guard: only run after all controls have been created.
static void ApplyFromControls(HWND hwnd)
{
    // If any required control doesn't exist yet, we're still in OpenSettingsWindow
    // building the layout — skip to avoid corrupting config with partial state.
    if (!GetDlgItem(hwnd, ID_CMB_PRESET) ||
        !GetDlgItem(hwnd, ID_CMB_MONITOR) ||
        !GetDlgItem(hwnd, ID_EDT_OFFSET))
        return;

    WCHAR text[32];

    // step
    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_STEP), text, 32);
    float step = (float)_wtof(text);
    if (step >= 0.1f)
        g_config.magStep = step;

    // frame rate — restart timer if changed
    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_FPS), text, 32);
    int fps = _wtoi(text);
    if (fps >= 1 && fps <= 360 && fps != g_config.frameRate)
    {
        g_config.frameRate = fps;
        KillTimer(hwndHost, g_timerId);
        g_timerId = SetTimer(hwndHost, 0, TimerInterval(), UpdateMagWindow);
    }

    // size
    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_WIDTH), text, 32);
    int width = _wtoi(text);
    if (width >= 50)
        g_config.windowWidth = width;
    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_HEIGHT), text, 32);
    int height = _wtoi(text);
    if (height >= 50)
        g_config.windowHeight = height;

    // monitor
    int selectedMonitor = (int)SendMessageW(GetDlgItem(hwnd, ID_CMB_MONITOR), CB_GETCURSEL, 0, 0);
    if (selectedMonitor >= 0)
        g_config.monitorIndex = selectedMonitor;

    // offset
    int presetSelection = (int)SendMessageW(GetDlgItem(hwnd, ID_CMB_PRESET), CB_GETCURSEL, 0, 0);
    g_config.offsetIsHunt = (presetSelection == 1);
    if (g_config.offsetIsHunt)
    {
        g_config.offsetY = HuntOffset();
    }
    else
    {
        GetWindowTextW(GetDlgItem(hwnd, ID_EDT_OFFSET), text, 32);
        g_config.offsetY = _wtoi(text);
    }

    // Keys are updated immediately in KeyEditProc — don't touch them here.

    ApplyOverlay();
    SaveConfig();
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD notif = HIWORD(wParam);

        if (id == ID_CMB_MONITOR && notif == CBN_SELCHANGE)
        {
            ApplyFromControls(hwnd);
        }

        if (id == ID_CMB_PRESET && notif == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(GetDlgItem(hwnd, ID_CMB_PRESET), CB_GETCURSEL, 0, 0);
            HWND offsetEdit = GetDlgItem(hwnd, ID_EDT_OFFSET);
            if (sel == 1)
            {
                WCHAR text[16];
                swprintf_s(text, L"%d", HuntOffset());
                SetWindowTextW(offsetEdit, text);
                EnableWindow(offsetEdit, FALSE);
            }
            else
            {
                EnableWindow(offsetEdit, TRUE);
            }
            ApplyFromControls(hwnd);
        }

        // Numeric edits — apply as you type (EN_CHANGE), only when value is valid.
        if ((id == ID_EDT_STEP || id == ID_EDT_WIDTH || id == ID_EDT_HEIGHT ||
             id == ID_EDT_OFFSET || id == ID_EDT_FPS) &&
            notif == EN_CHANGE)
        {
            WCHAR text[32];
            GetWindowTextW((HWND)lParam, text, 32);
            double v = _wtof(text);
            bool valid = (id == ID_EDT_STEP) ? v >= 0.1 : (id == ID_EDT_WIDTH) ? v >= 50
                                                      : (id == ID_EDT_HEIGHT)  ? v >= 50
                                                      : (id == ID_EDT_FPS)     ? (v >= 1 && v <= 360)
                                                                               : text[0] != L'\0';
            if (valid)
                ApplyFromControls(hwnd);
        }

        if (id == ID_BTN_CLOSE)
        {
            ApplyFromControls(hwnd);
            DestroyWindow(hwnd);
        }
        if (id == ID_BTN_EXIT)
        {
            DestroyWindow(hwnd);
            DestroyWindow(hwndHost);
        }
        break;
    }
    case WM_CLOSE:
        ApplyFromControls(hwnd);
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        g_hwndSettings = NULL;
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Helpers
static HWND MakeGroup(HWND p, const WCHAR *t, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"BUTTON", t,
                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                           x, y, w, h, p, NULL, hInst, NULL);
}
static HWND MakeLabel(HWND p, const WCHAR *t, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, h, p, NULL, hInst, NULL);
}
static HWND MakeEdit(HWND p, int id, const WCHAR *t, int x, int y, int w, int h)
{
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", t,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           x, y, w, h, p, (HMENU)(INT_PTR)id, hInst, NULL);
}
static HWND MakeCombo(HWND p, int id, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"COMBOBOX", NULL,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h, p, (HMENU)(INT_PTR)id, hInst, NULL);
}
static HWND MakeButton(HWND p, int id, const WCHAR *t, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"BUTTON", t,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           x, y, w, h, p, (HMENU)(INT_PTR)id, hInst, NULL);
}

void OpenSettingsWindow()
{
    if (g_hwndSettings)
    {
        SetForegroundWindow(g_hwndSettings);
        return;
    }

    static bool classReg = false;
    if (!classReg)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MagSettings";
        RegisterClassExW(&wc);
        classReg = true;
    }

    EnumerateMonitors();

    // Layout constants
    const int DIALOG_WIDTH = 370;
    const int GROUP_LEFT = 8;
    const int GROUP_WIDTH = DIALOG_WIDTH - 16;
    const int LABEL_LEFT = GROUP_LEFT + 10;
    const int LABEL_WIDTH = 80;
    const int EDIT_LEFT = LABEL_LEFT + LABEL_WIDTH + 4;
    const int EDIT_WIDTH = GROUP_WIDTH - LABEL_WIDTH - 40;
    const int ROW_HEIGHT = 26;
    const int GROUP_TITLE_PADDING = 18;
    const int GROUP_GAP = 6;

    const int SINGLE_ROW_GROUP_H = GROUP_TITLE_PADDING + ROW_HEIGHT + 8;
    const int DOUBLE_ROW_GROUP_H = GROUP_TITLE_PADDING + ROW_HEIGHT * 2 + 8;
    const int TRIPLE_ROW_GROUP_H = GROUP_TITLE_PADDING + ROW_HEIGHT * 3 + 8;

    int y = 6;

    int totalWindowHeight = y + TRIPLE_ROW_GROUP_H + GROUP_GAP + SINGLE_ROW_GROUP_H + GROUP_GAP + TRIPLE_ROW_GROUP_H + GROUP_GAP + DOUBLE_ROW_GROUP_H + GROUP_GAP + 30 + 8 + 38;

    g_hwndSettings = CreateWindowExW(WS_EX_TOPMOST,
                                     L"MagSettings", L"Magnifier Settings",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                     CW_USEDEFAULT, CW_USEDEFAULT, DIALOG_WIDTH, totalWindowHeight,
                                     hwndHost, NULL, hInst, NULL);

    // ── Shortcuts ────────────────────────────────────────────────────────────
    MakeGroup(g_hwndSettings, L"Shortcuts", GROUP_LEFT, y, GROUP_WIDTH, TRIPLE_ROW_GROUP_H);
    int rowY = y + GROUP_TITLE_PADDING;

    MakeLabel(g_hwndSettings, L"Toggle:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    HWND toggleKeyEdit = MakeEdit(g_hwndSettings, ID_EDT_TOGGLE, KeyName(g_config.toggleKey).c_str(), EDIT_LEFT, rowY, EDIT_WIDTH, 20);
    rowY += ROW_HEIGHT;

    MakeLabel(g_hwndSettings, L"Mag Up:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    HWND magUpKeyEdit = MakeEdit(g_hwndSettings, ID_EDT_MAGUP, KeyName(g_config.magUpKey).c_str(), EDIT_LEFT, rowY, EDIT_WIDTH, 20);
    rowY += ROW_HEIGHT;

    MakeLabel(g_hwndSettings, L"Mag Down:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    HWND magDownKeyEdit = MakeEdit(g_hwndSettings, ID_EDT_MAGDOWN, KeyName(g_config.magDownKey).c_str(), EDIT_LEFT, rowY, EDIT_WIDTH, 20);

    y += TRIPLE_ROW_GROUP_H + GROUP_GAP;

    // ── Magnification ────────────────────────────────────────────────────────
    MakeGroup(g_hwndSettings, L"Magnification", GROUP_LEFT, y, GROUP_WIDTH, SINGLE_ROW_GROUP_H);
    rowY = y + GROUP_TITLE_PADDING;

    MakeLabel(g_hwndSettings, L"Step:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    WCHAR stepText[16];
    swprintf_s(stepText, L"%.2f", g_config.magStep);
    MakeEdit(g_hwndSettings, ID_EDT_STEP, stepText, EDIT_LEFT, rowY, 60, 20);

    y += SINGLE_ROW_GROUP_H + GROUP_GAP;

    // ── Overlay ──────────────────────────────────────────────────────────────
    MakeGroup(g_hwndSettings, L"Overlay", GROUP_LEFT, y, GROUP_WIDTH, TRIPLE_ROW_GROUP_H);
    rowY = y + GROUP_TITLE_PADDING;

    MakeLabel(g_hwndSettings, L"Size:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    WCHAR widthText[8], heightText[8];
    swprintf_s(widthText, L"%d", g_config.windowWidth);
    swprintf_s(heightText, L"%d", g_config.windowHeight);
    MakeEdit(g_hwndSettings, ID_EDT_WIDTH, widthText, EDIT_LEFT, rowY, 55, 20);
    MakeLabel(g_hwndSettings, L"x", EDIT_LEFT + 60, rowY + 3, 12, 16);
    MakeEdit(g_hwndSettings, ID_EDT_HEIGHT, heightText, EDIT_LEFT + 75, rowY, 55, 20);
    rowY += ROW_HEIGHT;

    MakeLabel(g_hwndSettings, L"FPS:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    WCHAR fpsText[8];
    swprintf_s(fpsText, L"%d", g_config.frameRate);
    MakeEdit(g_hwndSettings, ID_EDT_FPS, fpsText, EDIT_LEFT, rowY, 60, 20);
    rowY += ROW_HEIGHT;

    MakeLabel(g_hwndSettings, L"Monitor:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    HWND monitorCombo = MakeCombo(g_hwndSettings, ID_CMB_MONITOR, EDIT_LEFT, rowY, EDIT_WIDTH, 200);
    for (int i = 0; i < (int)g_monitors.size(); i++)
    {
        RECT &r = g_monitors[i].mi.rcMonitor;
        bool primary = (g_monitors[i].mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        WCHAR monitorLabel[64];
        swprintf_s(monitorLabel, L"Monitor %d  %dx%d%s", i + 1,
                   r.right - r.left, r.bottom - r.top, primary ? L" *" : L"");
        SendMessageW(monitorCombo, CB_ADDSTRING, 0, (LPARAM)monitorLabel);
    }
    int validMonitorIndex = g_config.monitorIndex;
    if (validMonitorIndex >= (int)g_monitors.size())
        validMonitorIndex = 0;
    SendMessageW(monitorCombo, CB_SETCURSEL, validMonitorIndex, 0);

    y += TRIPLE_ROW_GROUP_H + GROUP_GAP;

    // ── Y Offset ─────────────────────────────────────────────────────────────
    MakeGroup(g_hwndSettings, L"Y Offset", GROUP_LEFT, y, GROUP_WIDTH, DOUBLE_ROW_GROUP_H);
    rowY = y + GROUP_TITLE_PADDING;

    MakeLabel(g_hwndSettings, L"Preset:", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    HWND presetCombo = MakeCombo(g_hwndSettings, ID_CMB_PRESET, EDIT_LEFT, rowY, EDIT_WIDTH, 100);
    SendMessageW(presetCombo, CB_ADDSTRING, 0, (LPARAM)L"Custom");
    SendMessageW(presetCombo, CB_ADDSTRING, 0, (LPARAM)L"Hunt: Showdown");
    // Todo add center offset, and others in future updatesyy
    SendMessageW(presetCombo, CB_SETCURSEL, g_config.offsetIsHunt ? 1 : 0, 0);
    rowY += ROW_HEIGHT;

    MakeLabel(g_hwndSettings, L"Value (px):", LABEL_LEFT, rowY + 3, LABEL_WIDTH, 16);
    WCHAR offsetText[16];
    swprintf_s(offsetText, L"%d", g_config.offsetY);
    HWND offsetEdit = MakeEdit(g_hwndSettings, ID_EDT_OFFSET, offsetText, EDIT_LEFT, rowY, 60, 20);
    if (g_config.offsetIsHunt)
        EnableWindow(offsetEdit, FALSE);

    y += DOUBLE_ROW_GROUP_H + GROUP_GAP;

    // ── Buttons ──────────────────────────────────────────────────────────────
    MakeButton(g_hwndSettings, ID_BTN_CLOSE, L"Close", GROUP_LEFT, y, 80, 26);
    MakeButton(g_hwndSettings, ID_BTN_EXIT, L"Exit App", GROUP_WIDTH - 80 + GROUP_LEFT, y, 80, 26);

    // Subclass key-capture fields
    g_keyCaptures[0] = {toggleKeyEdit, g_config.toggleKey, false,
                        (WNDPROC)SetWindowLongPtrW(toggleKeyEdit, GWLP_WNDPROC, (LONG_PTR)KeyEditProc),
                        &g_config.toggleKey};
    g_keyCaptures[1] = {magUpKeyEdit, g_config.magUpKey, false,
                        (WNDPROC)SetWindowLongPtrW(magUpKeyEdit, GWLP_WNDPROC, (LONG_PTR)KeyEditProc),
                        &g_config.magUpKey};
    g_keyCaptures[2] = {magDownKeyEdit, g_config.magDownKey, false,
                        (WNDPROC)SetWindowLongPtrW(magDownKeyEdit, GWLP_WNDPROC, (LONG_PTR)KeyEditProc),
                        &g_config.magDownKey};

    SetForegroundWindow(g_hwndSettings);
}

// ─── Host window proc ────────────────────────────────────────────────────────

LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        RemoveTray();
        PostQuitMessage(0);
        break;

    case WM_SIZE:
        if (hwndMag)
        {
            GetClientRect(hWnd, &magWindowRect);
            SetWindowPos(hwndMag, NULL,
                         magWindowRect.left, magWindowRect.top,
                         magWindowRect.right, magWindowRect.bottom, 0);
        }
        break;

    case WM_TRAY:
        if (lParam == WM_LBUTTONDBLCLK)
        {
            OpenSettingsWindow();
        }
        else if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"Settings");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(menu);
            if (cmd == ID_TRAY_SETTINGS)
                OpenSettingsWindow();
            if (cmd == ID_TRAY_EXIT)
                DestroyWindow(hWnd);
        }
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ─── Keyboard hook ───────────────────────────────────────────────────────────

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN)
    {
        DWORD vk = ((KBDLLHOOKSTRUCT *)lParam)->vkCode;

        if (vk == g_config.toggleKey)
        {
            g_overlayVisible = !g_overlayVisible;
            SetLayeredWindowAttributes(hwndHost, 0, g_overlayVisible ? 255 : 0, LWA_ALPHA);
        }
        if (vk == g_config.magUpKey)
        {
            g_magFactor += g_config.magStep;
            MAGTRANSFORM m = {};
            m.v[0][0] = m.v[1][1] = g_magFactor;
            m.v[2][2] = 1.0f;
            MagSetWindowTransform(hwndMag, &m);
        }
        if (vk == g_config.magDownKey)
        {
            if (g_magFactor - g_config.magStep >= 1.0f)
                g_magFactor -= g_config.magStep;
            MAGTRANSFORM m = {};
            m.v[0][0] = m.v[1][1] = g_magFactor;
            m.v[2][2] = 1.0f;
            MagSetWindowTransform(hwndMag, &m);
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ─── Magnifier setup ─────────────────────────────────────────────────────────

ATOM RegisterHostWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = HostWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
    wcex.lpszClassName = WindowClassName;
    return RegisterClassExW(&wcex);
}

BOOL SetupMagnifier(HINSTANCE hinst)
{
    RegisterHostWindowClass(hinst);

    RECT mr = GetMonitorRect(g_config.monitorIndex);
    int cx = mr.left + (mr.right - mr.left) / 2;
    int cy = mr.top + (mr.bottom - mr.top) / 2;

    hwndHost = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT,
                               WindowClassName, L"Screen Magnifier", WS_POPUP,
                               cx - g_config.windowWidth / 2,
                               cy + g_config.offsetY - g_config.windowHeight / 2,
                               g_config.windowWidth, g_config.windowHeight,
                               NULL, NULL, hInst, NULL);
    if (!hwndHost)
        return FALSE;

    SetLayeredWindowAttributes(hwndHost, 0, 0, LWA_ALPHA);

    GetClientRect(hwndHost, &magWindowRect);
    hwndMag = CreateWindowExW(0, WC_MAGNIFIERW, L"MagnifierWindow",
                              WS_CHILD | WS_VISIBLE,
                              magWindowRect.left, magWindowRect.top,
                              magWindowRect.right, magWindowRect.bottom,
                              hwndHost, NULL, hInst, NULL);
    if (!hwndMag)
        return FALSE;

    MAGTRANSFORM m = {};
    m.v[0][0] = m.v[1][1] = g_magFactor;
    m.v[2][2] = 1.0f;
    return MagSetWindowTransform(hwndMag, &m);
}

void CALLBACK UpdateMagWindow(HWND, UINT, UINT_PTR, DWORD)
{
    if (!g_overlayVisible)
        return;

    RECT mr = GetMonitorRect(g_config.monitorIndex);
    int cx = mr.left + (mr.right - mr.left) / 2;
    int cy = mr.top + (mr.bottom - mr.top) / 2;

    int srcWidth = (int)((magWindowRect.right - magWindowRect.left) / g_magFactor);
    int srcHeight = (int)((magWindowRect.bottom - magWindowRect.top) / g_magFactor);

    RECT src;
    src.left = cx - srcWidth / 2;
    src.top = cy + g_config.offsetY + (long)g_magFactor - srcHeight / 2;
    src.right = src.left + srcWidth;
    src.bottom = src.top + srcHeight;

    MagSetWindowSource(hwndMag, src);
    // Skip topmost reassertion while settings window is open — it dismisses combo dropdowns
    if (!g_hwndSettings)
        SetWindowPos(hwndHost, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    InvalidateRect(hwndMag, NULL, TRUE);
}

// ─── Entry point ─────────────────────────────────────────────────────────────

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE,
                     _In_ LPSTR, _In_ int nCmdShow)
{
    hInst = hInstance;
    LoadConfig();
    EnumerateMonitors();

    if (!MagInitialize())
        return 0;
    if (!SetupMagnifier(hInstance))
        return 0;

    ShowWindow(hwndHost, nCmdShow);
    UpdateWindow(hwndHost);
    SetupTray(hwndHost);

    g_timerId = SetTimer(hwndHost, 0, TimerInterval(), UpdateMagWindow);
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hook);
    KillTimer(hwndHost, g_timerId);
    MagUninitialize();
    return (int)msg.wParam;
}
