/*
 * UASTWin.c - UASTWindows.exe (图形前端)
 *
 * 作者: 明明月明月11019 (哔哩哔哩)
 *       https://space.bilibili.com/3707056078982013
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wchar.h>

#include "../Common/translation.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

static const WCHAR* APP_CLASS = L"UASTWindowsWndClass";
static const WCHAR* HELP_CLASS = L"UASTHelpWndClass";

enum {
    IDC_EDIT_TARGET = 1001,
    IDC_BTN_BROWSE,
    IDC_COMBO_USER,
    IDC_COMBO_PRIV,
    IDC_COMBO_INTEG,
    IDC_COMBO_PRIORITY,
    IDC_COMBO_WINDOW,
    IDC_CHECK_WAIT,
    IDC_COMBO_LANGUAGE,
    IDC_BTN_RUN,
    IDC_BTN_HELP,
    IDC_STATUS,
};

typedef struct _LaunchOptions {
    WCHAR target[MAX_PATH * 4];
    WCHAR currentDirectory[MAX_PATH];
    int   userMode;
    int   privMode;
    int   integMode;
    int   priorityMode;
    int   windowMode;
    BOOL  wait;
} LaunchOptions;

static HFONT g_hFont = NULL;
static HWND  g_hWnd = NULL;
static BOOL  g_bHelpClosed = FALSE;
static BOOL  g_bHelpExit = FALSE;

static void SetStatus(LPCWSTR text)
{
    SetDlgItemTextW(g_hWnd, IDC_STATUS, text);
}

static BOOL LaunchUASTCmdup(const LaunchOptions* opt)
{
    WCHAR args[MAX_PATH * 10] = { 0 };
    WCHAR selfPath[MAX_PATH] = { 0 };
    WCHAR enginePath[MAX_PATH] = { 0 };
    WCHAR cmdLine[MAX_PATH * 12] = { 0 };
    WCHAR* slash;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    BOOL ok;

    switch (opt->userMode) {
    case 0: wcscat_s(args, _countof(args), L" -U:E"); break;
    case 1: wcscat_s(args, _countof(args), L" -U:S"); break;
    case 2: wcscat_s(args, _countof(args), L" -U:T"); break;
    case 3: wcscat_s(args, _countof(args), L" -U:C"); break;
    }
    switch (opt->privMode) {
    case 1: wcscat_s(args, _countof(args), L" -P:E"); break;
    case 2: wcscat_s(args, _countof(args), L" -P:D"); break;
    }
    switch (opt->integMode) {
    case 1: wcscat_s(args, _countof(args), L" -M:S"); break;
    case 2: wcscat_s(args, _countof(args), L" -M:H"); break;
    case 3: wcscat_s(args, _countof(args), L" -M:M"); break;
    case 4: wcscat_s(args, _countof(args), L" -M:L"); break;
    }
    switch (opt->priorityMode) {
    case 1: wcscat_s(args, _countof(args), L" -Priority:RealTime"); break;
    case 2: wcscat_s(args, _countof(args), L" -Priority:High"); break;
    case 3: wcscat_s(args, _countof(args), L" -Priority:AboveNormal"); break;
    case 4: wcscat_s(args, _countof(args), L" -Priority:Normal"); break;
    case 5: wcscat_s(args, _countof(args), L" -Priority:BelowNormal"); break;
    case 6: wcscat_s(args, _countof(args), L" -Priority:Idle"); break;
    }
    switch (opt->windowMode) {
    case 0: wcscat_s(args, _countof(args), L" -ShowWindowMode:Show"); break;
    case 1: wcscat_s(args, _countof(args), L" -ShowWindowMode:Hide"); break;
    case 2: wcscat_s(args, _countof(args), L" -ShowWindowMode:Maximize"); break;
    case 3: wcscat_s(args, _countof(args), L" -ShowWindowMode:Minimize"); break;
    }
    if (opt->currentDirectory[0]) {
        wcscat_s(args, _countof(args), L" -CurrentDirectory:\"");
        wcscat_s(args, _countof(args), opt->currentDirectory);
        wcscat_s(args, _countof(args), L"\"");
    }
    if (opt->wait) wcscat_s(args, _countof(args), L" -Wait");

    wcscat_s(args, _countof(args), L" \"");
    wcscat_s(args, _countof(args), opt->target);
    wcscat_s(args, _countof(args), L"\"");

    if (!GetModuleFileNameW(NULL, selfPath, MAX_PATH)) return FALSE;
    wcscpy_s(enginePath, _countof(enginePath), selfPath);
    slash = wcsrchr(enginePath, L'\\');
    if (!slash) return FALSE;
    *(slash + 1) = L'\0';
    wcscat_s(enginePath, _countof(enginePath), L"UASTCmdup.exe");

    swprintf_s(cmdLine, _countof(cmdLine), L"\"%s\"%s", enginePath, args);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    ok = CreateProcessW(enginePath, cmdLine, NULL, NULL, FALSE,
        CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
        NULL, NULL, &si, &pi);

    if (!ok) return FALSE;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

/* 创建单个下拉框 (简化) */
static HWND MakeCombo(HWND hWnd, int id, int x, int y, int w)
{
    HWND h = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, 200, hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return h;
}

static HWND MakeLabel(HWND hWnd, LPCWSTR text, int x, int y, int w)
{
    HWND h = CreateWindowW(L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return h;
}

static void CreateControls(HWND hWnd)
{
    const int MARGIN_X = 20;
    const int LABEL_W = 90;
    const int COMBO_W = 160;
    const int ROW_H = 30;
    int y = 60;
    HWND h;

    g_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    /* 标题 */
    MakeLabel(hWnd, Tr(L"gui.title"), MARGIN_X, 20, 460);

    /* 目标程序 */
    MakeLabel(hWnd, Tr(L"gui.target"), MARGIN_X, y, LABEL_W);
    h = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        MARGIN_X + LABEL_W, y - 2, 280, 24,
        hWnd, (HMENU)(INT_PTR)IDC_EDIT_TARGET, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"BUTTON", Tr(L"gui.browse"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        MARGIN_X + LABEL_W + 290, y - 2, 70, 24,
        hWnd, (HMENU)(INT_PTR)IDC_BTN_BROWSE, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    y += ROW_H + 5;

    /* 运行身份 + 特权 */
    MakeLabel(hWnd, Tr(L"gui.user"), MARGIN_X, y, LABEL_W);
    h = MakeCombo(hWnd, IDC_COMBO_USER, MARGIN_X + LABEL_W, y - 2, COMBO_W);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.user.admin"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.user.system"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.user.ti"));
    SendMessageW(h, CB_SETCURSEL, 0, 0);

    MakeLabel(hWnd, Tr(L"gui.priv"), MARGIN_X + LABEL_W + COMBO_W + 30, y, 50);
    h = MakeCombo(hWnd, IDC_COMBO_PRIV, MARGIN_X + LABEL_W + COMBO_W + 80, y - 2, 130);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.priv.default"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.priv.enable"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.priv.disable"));
    SendMessageW(h, CB_SETCURSEL, 0, 0);
    y += ROW_H;

    /* 完整性 + 优先级 */
    MakeLabel(hWnd, Tr(L"gui.integ"), MARGIN_X, y, LABEL_W);
    h = MakeCombo(hWnd, IDC_COMBO_INTEG, MARGIN_X + LABEL_W, y - 2, COMBO_W);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.integ.default"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.integ.system"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.integ.high"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.integ.medium"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.integ.low"));
    SendMessageW(h, CB_SETCURSEL, 0, 0);

    MakeLabel(hWnd, Tr(L"gui.prio"), MARGIN_X + LABEL_W + COMBO_W + 30, y, 50);
    h = MakeCombo(hWnd, IDC_COMBO_PRIORITY, MARGIN_X + LABEL_W + COMBO_W + 80, y - 2, 130);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.default"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.realtime"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.high"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.above"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.normal"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.below"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.prio.idle"));
    SendMessageW(h, CB_SETCURSEL, 0, 0);
    y += ROW_H;

    /* 窗口模式 + 等待 */
    MakeLabel(hWnd, Tr(L"gui.window"), MARGIN_X, y, LABEL_W);
    h = MakeCombo(hWnd, IDC_COMBO_WINDOW, MARGIN_X + LABEL_W, y - 2, COMBO_W);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.window.show"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.window.hide"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.window.max"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"gui.window.min"));
    SendMessageW(h, CB_SETCURSEL, 0, 0);

    h = CreateWindowW(L"BUTTON", Tr(L"gui.wait"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        MARGIN_X + LABEL_W + COMBO_W + 30, y, 180, 24,
        hWnd, (HMENU)(INT_PTR)IDC_CHECK_WAIT, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    y += ROW_H + 5;

    /* 语言 */
    MakeLabel(hWnd, Tr(L"gui.language"), MARGIN_X, y, LABEL_W);
    h = MakeCombo(hWnd, IDC_COMBO_LANGUAGE, MARGIN_X + LABEL_W, y - 2, COMBO_W * 2);
    {
        int cur = 0;
        for (int i = 0; i < TrLanguageCount(); i++) {
            SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)TrLanguageName(i));
            if (_wcsicmp(TrLanguageCode(i), TrCurrentCode()) == 0) cur = i;
        }
        SendMessageW(h, CB_SETCURSEL, cur, 0);
    }
    y += ROW_H + 10;

    /* 状态栏 */
    h = CreateWindowW(L"STATIC", Tr(L"gui.status.ready"),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, y, 460, 20,
        hWnd, (HMENU)(INT_PTR)IDC_STATUS, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    y += 26;

    /* 按钮 */
    h = CreateWindowW(L"BUTTON", Tr(L"gui.btn.run"),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        MARGIN_X, y, 350, 36,
        hWnd, (HMENU)(INT_PTR)IDC_BTN_RUN, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"BUTTON", Tr(L"gui.btn.help"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        MARGIN_X + 360, y, 100, 36,
        hWnd, (HMENU)(INT_PTR)IDC_BTN_HELP, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

static void ReadOptions(HWND hWnd, LaunchOptions* opt)
{
    ZeroMemory(opt, sizeof(LaunchOptions));
    GetDlgItemTextW(hWnd, IDC_EDIT_TARGET,
        opt->target, (int)_countof(opt->target));
    opt->userMode = (int)SendDlgItemMessageW(hWnd, IDC_COMBO_USER, CB_GETCURSEL, 0, 0);
    opt->privMode = (int)SendDlgItemMessageW(hWnd, IDC_COMBO_PRIV, CB_GETCURSEL, 0, 0);
    opt->integMode = (int)SendDlgItemMessageW(hWnd, IDC_COMBO_INTEG, CB_GETCURSEL, 0, 0);
    opt->priorityMode = (int)SendDlgItemMessageW(hWnd, IDC_COMBO_PRIORITY, CB_GETCURSEL, 0, 0);
    opt->windowMode = (int)SendDlgItemMessageW(hWnd, IDC_COMBO_WINDOW, CB_GETCURSEL, 0, 0);
    opt->wait = (IsDlgButtonChecked(hWnd, IDC_CHECK_WAIT) == BST_CHECKED);
}

static LRESULT CALLBACK HelpWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            g_bHelpClosed = TRUE;
            DestroyWindow(hWnd);
            return 0;
        case IDCANCEL:
            g_bHelpClosed = TRUE;
            g_bHelpExit = TRUE;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        g_bHelpClosed = TRUE;
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ShowHelpDialog(HWND hParent)
{
    HWND hDlg, hEdit, hClose, hExit;
    HFONT hMono = NULL;
    MSG msg;
    RECT rc, rcParent;
    int w, h, px, py;
    WNDCLASSEXW wc;
    static BOOL s_classRegistered = FALSE;

    if (!s_classRegistered) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = HelpWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = HELP_CLASS;
        if (!RegisterClassExW(&wc)) {
            MessageBoxW(hParent, Tr(L"gui.help.text"),
                Tr(L"gui.help.title"), MB_OK | MB_ICONINFORMATION);
            return;
        }
        s_classRegistered = TRUE;
    }

    g_bHelpClosed = FALSE;
    g_bHelpExit = FALSE;

    hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME,
        HELP_CLASS, Tr(L"gui.help.title"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 620,
        hParent, NULL, GetModuleHandle(NULL), NULL);
    if (!hDlg) return;

    hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        Tr(L"gui.help.text"),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        10, 10, 680, 540,
        hDlg, NULL, GetModuleHandle(NULL), NULL);

    hMono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hMono, TRUE);

    hClose = CreateWindowW(L"BUTTON", Tr(L"gui.btn.close"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        520, 560, 80, 30, hDlg, (HMENU)IDOK, NULL, NULL);
    SendMessage(hClose, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    hExit = CreateWindowW(L"BUTTON", Tr(L"gui.btn.exit"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        610, 560, 80, 30, hDlg, (HMENU)IDCANCEL, NULL, NULL);
    SendMessage(hExit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    GetWindowRect(hDlg, &rc);
    GetWindowRect(hParent, &rcParent);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    px = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    py = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;
    SetWindowPos(hDlg, NULL, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    while (!g_bHelpClosed && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hMono) DeleteObject(hMono);
    if (g_bHelpExit) PostQuitMessage(0);
}

static void OnRunClicked(HWND hWnd)
{
    LaunchOptions opt;
    ReadOptions(hWnd, &opt);
    if (!opt.target[0]) {
        MessageBoxW(hWnd, Tr(L"gui.err.no_target"),
            Tr(L"gui.err.no_target.title"), MB_OK | MB_ICONWARNING);
        return;
    }
    if (LaunchUASTCmdup(&opt)) {
        SetStatus(Tr(L"gui.status.calling"));
    }
    else {
        SetStatus(Tr(L"gui.status.failed"));
        MessageBoxW(hWnd, Tr(L"gui.err.engine_not_found"),
            Tr(L"gui.err.title"), MB_OK | MB_ICONERROR);
    }
}

static void OnBrowseClicked(HWND hWnd)
{
    WCHAR path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Executable\0*.exe;*.bat;*.cmd\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        SetDlgItemTextW(hWnd, IDC_EDIT_TARGET, path);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hWnd = hWnd;
        CreateControls(hWnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_RUN:    OnRunClicked(hWnd);    return 0;
        case IDC_BTN_BROWSE: OnBrowseClicked(hWnd); return 0;
        case IDC_BTN_HELP:   ShowHelpDialog(hWnd);  return 0;
        case IDC_COMBO_LANGUAGE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageW(hWnd, IDC_COMBO_LANGUAGE,
                    CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < TrLanguageCount()) {
                    LPCWSTR code = TrLanguageCode(sel);
                    if (TrSetLanguage(code)) {
                        MessageBoxW(hWnd, Tr(L"gui.lang.restart"),
                            Tr(L"gui.err.no_target.title"),
                            MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    HWND hWnd;
    RECT rc;
    int w, h, sw, sh;
    MSG msg;
    HRESULT hrCom;
    WCHAR selfPath[MAX_PATH] = { 0 };
    WCHAR jsonPath[MAX_PATH] = { 0 };

    (void)hPrevInstance;
    (void)lpCmdLine;

    hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* 初始化翻译 */
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    {
        WCHAR* slash = wcsrchr(selfPath, L'\\');
        if (slash) {
            *(slash + 1) = L'\0';
            swprintf_s(jsonPath, MAX_PATH, L"%slanguage.json", selfPath);
        }
    }
    TrInit(jsonPath);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APP_CLASS;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"RegisterClass failed", L"Error", MB_OK | MB_ICONERROR);
        TrShutdown();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    hWnd = CreateWindowExW(0, APP_CLASS, Tr(L"gui.title"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 440,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBoxW(NULL, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        TrShutdown();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    GetWindowRect(hWnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hWnd, NULL, (sw - w) / 2, (sh - h) / 2, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    TrShutdown();
    if (SUCCEEDED(hrCom)) CoUninitialize();
    return (int)msg.wParam;
}