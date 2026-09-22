/*
 * UASTWindows.exe - UASTCmdup 的图形前端
 *
 * 定位:
 *   提供图形界面, 收集用户选项后拼装命令行参数, 通过 CreateProcessW
 *   启动 UASTCmdup.exe 完成实际的提权操作。使用 CREATE_NEW_CONSOLE
 *   标志确保引擎拥有独立的控制台窗口显示日志。
 *
 * 本程序自身不提权, 不以管理员身份运行。所有提权逻辑由 UASTCmdup.exe
 * 负责。
 *
 * 依赖:
 *   UASTCmdup.exe 必须与本程序位于同一目录。
 *
 * 作者: 明明月明月11019 (哔哩哔哩)
 *        https://space.bilibili.com/3707056078982013
 *
 * 许可证: 详见 LICENSE 文件
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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

 // ============================================================================
 // 常量
 // ============================================================================

static const WCHAR* APP_TITLE = L"UASTWindows - 图形化提权工具";
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
    IDC_BTN_RUN,
    IDC_BTN_HELP,
    IDC_STATUS,
};

// ============================================================================
// 帮助文本
// ============================================================================

static const WCHAR* HELP_TEXT =
L"UASTWindows - 图形化提权工具\r\n"
L"\r\n"
L"本程序是 UASTCmdup.exe 的图形前端。\r\n"
L"所有提权操作由 UASTCmdup.exe 完成。\r\n"
L"\r\n"
L"界面说明:\r\n"
L"  目标程序       要执行的程序或命令行\r\n"
L"  运行身份       管理员 / SYSTEM / TrustedInstaller\r\n"
L"  特权           默认 / 启用全部 / 禁用全部\r\n"
L"  完整性         System / High / Medium / Low\r\n"
L"  优先级         实时 / 高 / 高于正常 / 正常 / 低于正常 / 低\r\n"
L"  窗口模式       显示 / 隐藏 / 最大化 / 最小化\r\n"
L"  等待进程结束   勾选后, 引擎会等待子进程结束再退出\r\n"
L"\r\n"
L"命令行用法 (直接调用 UASTCmdup.exe):\r\n"
L"  UASTCmdup.exe [选项] 命令行\r\n"
L"\r\n"
L"选项:\r\n"
L"  -U:[T|S|C|E]          指定运行身份\r\n"
L"  -P:[E|D]              指定令牌特权\r\n"
L"  -M:[S|H|M|L]          指定完整性级别\r\n"
L"  -Priority:[...]       指定进程优先级\r\n"
L"  -ShowWindowMode:[...] 指定窗口显示模式\r\n"
L"  -CurrentDirectory:[]  指定工作目录\r\n"
L"  -Wait                 等待子进程结束\r\n"
L"  -Version              显示版本信息\r\n"
L"  -?  -H  -Help         显示帮助\r\n"
L"\r\n"
L"示例:\r\n"
L"  UASTCmdup.exe -U:S cmd.exe\r\n"
L"  UASTCmdup.exe -U:T -P:E -M:S -Priority:RealTime cmd.exe /c whoami\r\n";

// ============================================================================
// 执行选项
// ============================================================================

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

// ============================================================================
// 全局状态
// ============================================================================

static HFONT g_hFont = NULL;
static HWND  g_hWnd = NULL;

static BOOL g_bHelpClosed = FALSE;
static BOOL g_bHelpExit = FALSE;

// ============================================================================
// 工具
// ============================================================================

static void SetStatus(LPCWSTR text)
{
    SetDlgItemTextW(g_hWnd, IDC_STATUS, text);
}

// ============================================================================
// 启动 UASTCmdup.exe
// ============================================================================

/**
 * 根据用户选项拼装命令行参数, 定位并启动 UASTCmdup.exe。
 *
 * 使用 CreateProcessW + CREATE_NEW_CONSOLE 而不是 ShellExecuteExW,
 * 以确保引擎获得独立的控制台窗口显示日志。
 *
 * @param opt  用户选择的选项
 * @return     TRUE 表示已成功启动; FALSE 表示启动失败
 */
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

    // ---- 拼参数 ----
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

    if (opt->wait) {
        wcscat_s(args, _countof(args), L" -Wait");
    }

    wcscat_s(args, _countof(args), L" \"");
    wcscat_s(args, _countof(args), opt->target);
    wcscat_s(args, _countof(args), L"\"");

    // ---- 定位引擎路径 (与 GUI 同目录) ----
    if (!GetModuleFileNameW(NULL, selfPath, MAX_PATH)) return FALSE;

    wcscpy_s(enginePath, _countof(enginePath), selfPath);
    slash = wcsrchr(enginePath, L'\\');
    if (!slash) return FALSE;

    *(slash + 1) = L'\0';
    wcscat_s(enginePath, _countof(enginePath), L"UASTCmdup.exe");

    // ---- 拼完整命令行 ----
    swprintf_s(cmdLine, _countof(cmdLine), L"\"%s\"%s", enginePath, args);

    // ---- CreateProcessW + CREATE_NEW_CONSOLE ----
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    ZeroMemory(&pi, sizeof(pi));

    ok = CreateProcessW(
        enginePath,                              // 应用程序路径
        cmdLine,                                 // 命令行 (可写缓冲区)
        NULL, NULL,                              // 默认安全属性
        FALSE,                                   // 不继承句柄
        CREATE_NEW_CONSOLE |                     // 创建独立控制台
        CREATE_UNICODE_ENVIRONMENT,              // Unicode 环境块
        NULL,                                    // 继承父环境块
        NULL,                                    // 继承父工作目录
        &si, &pi);

    if (!ok) return FALSE;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

// ============================================================================
// 图形界面
// ============================================================================

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

    // 标题
    h = CreateWindowW(L"STATIC", APP_TITLE,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, 20, 460, 26, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    // 目标程序行
    h = CreateWindowW(L"STATIC", L"目标程序:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, y, LABEL_W, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        MARGIN_X + LABEL_W, y - 2, 280, 24,
        hWnd, (HMENU)(INT_PTR)IDC_EDIT_TARGET, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"BUTTON", L"浏览...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        MARGIN_X + LABEL_W + 290, y - 2, 70, 24,
        hWnd, (HMENU)(INT_PTR)IDC_BTN_BROWSE, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    y += ROW_H + 5;

    // 运行身份 + 特权
    h = CreateWindowW(L"STATIC", L"运行身份:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, y, LABEL_W, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        MARGIN_X + LABEL_W, y - 2, COMBO_W, 200,
        hWnd, (HMENU)(INT_PTR)IDC_COMBO_USER, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"管理员");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"SYSTEM");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"TrustedInstaller");
    SendMessageW(h, CB_SETCURSEL, 0, 0);

    h = CreateWindowW(L"STATIC", L"特权:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X + LABEL_W + COMBO_W + 30, y, 50, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        MARGIN_X + LABEL_W + COMBO_W + 80, y - 2, 130, 200,
        hWnd, (HMENU)(INT_PTR)IDC_COMBO_PRIV, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"默认");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"启用全部");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"禁用全部");
    SendMessageW(h, CB_SETCURSEL, 0, 0);
    y += ROW_H;

    // 完整性 + 优先级
    h = CreateWindowW(L"STATIC", L"完整性:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, y, LABEL_W, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        MARGIN_X + LABEL_W, y - 2, COMBO_W, 200,
        hWnd, (HMENU)(INT_PTR)IDC_COMBO_INTEG, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"默认");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"System");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"High");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Medium");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Low");
    SendMessageW(h, CB_SETCURSEL, 0, 0);

    h = CreateWindowW(L"STATIC", L"优先级:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X + LABEL_W + COMBO_W + 30, y, 50, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        MARGIN_X + LABEL_W + COMBO_W + 80, y - 2, 130, 200,
        hWnd, (HMENU)(INT_PTR)IDC_COMBO_PRIORITY, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"默认");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"实时");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"高");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"高于正常");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"正常");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"低于正常");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"低");
    SendMessageW(h, CB_SETCURSEL, 0, 0);
    y += ROW_H;

    // 窗口模式 + 等待
    h = CreateWindowW(L"STATIC", L"窗口模式:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, y, LABEL_W, 20, hWnd, NULL, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        MARGIN_X + LABEL_W, y - 2, COMBO_W, 200,
        hWnd, (HMENU)(INT_PTR)IDC_COMBO_WINDOW, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"显示");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"隐藏");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"最大化");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"最小化");
    SendMessageW(h, CB_SETCURSEL, 0, 0);

    h = CreateWindowW(L"BUTTON", L"等待进程结束",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        MARGIN_X + LABEL_W + COMBO_W + 30, y, 180, 24,
        hWnd, (HMENU)(INT_PTR)IDC_CHECK_WAIT, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    y += ROW_H + 10;

    // 状态栏
    h = CreateWindowW(L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN_X, y, 460, 20,
        hWnd, (HMENU)(INT_PTR)IDC_STATUS, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    y += 26;

    // 按钮
    h = CreateWindowW(L"BUTTON", L"执行提权",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        MARGIN_X, y, 350, 36,
        hWnd, (HMENU)(INT_PTR)IDC_BTN_RUN, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    h = CreateWindowW(L"BUTTON", L"帮助",
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

/**
 * 帮助对话框窗口过程。
 * 处理"关闭"、"退出程序"和标题栏 X。
 */
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

/**
 * 帮助对话框。使用专用窗口类 UASTHelpWndClass, 使按钮和 X 能响应。
 */
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
            MessageBoxW(hParent, HELP_TEXT, L"帮助", MB_OK | MB_ICONINFORMATION);
            return;
        }
        s_classRegistered = TRUE;
    }

    g_bHelpClosed = FALSE;
    g_bHelpExit = FALSE;

    hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        HELP_CLASS, L"帮助",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 620,
        hParent, NULL, GetModuleHandle(NULL), NULL);

    if (!hDlg) {
        MessageBoxW(hParent, HELP_TEXT, L"帮助", MB_OK | MB_ICONINFORMATION);
        return;
    }

    hEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", HELP_TEXT,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        10, 10, 680, 540,
        hDlg, NULL, GetModuleHandle(NULL), NULL);

    hMono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hMono, TRUE);

    hClose = CreateWindowW(L"BUTTON", L"关闭",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        520, 560, 80, 30,
        hDlg, (HMENU)IDOK, NULL, NULL);
    SendMessage(hClose, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    hExit = CreateWindowW(L"BUTTON", L"退出程序",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        610, 560, 80, 30,
        hDlg, (HMENU)IDCANCEL, NULL, NULL);
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

    if (g_bHelpExit) {
        PostQuitMessage(0);
    }
}

/** "执行提权"按钮点击处理: 调用 UASTCmdup.exe。 */
static void OnRunClicked(HWND hWnd)
{
    LaunchOptions opt;

    ReadOptions(hWnd, &opt);

    if (!opt.target[0]) {
        MessageBoxW(hWnd, L"请先指定目标程序", L"提示",
            MB_OK | MB_ICONWARNING);
        return;
    }

    if (LaunchUASTCmdup(&opt)) {
        SetStatus(L"已调用 UASTCmdup, 请查看引擎窗口");
    }
    else {
        SetStatus(L"调用 UASTCmdup 失败");
        MessageBoxW(hWnd,
            L"无法启动 UASTCmdup.exe\n"
            L"请确保它与 UASTWindows.exe 位于同一目录。",
            L"错误", MB_OK | MB_ICONERROR);
    }
}

/** "浏览..."按钮点击处理。 */
static void OnBrowseClicked(HWND hWnd)
{
    WCHAR path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"可执行文件\0*.exe;*.bat;*.cmd\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(hWnd, IDC_EDIT_TARGET, path);
    }
}

/** 主窗口消息处理。 */
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
        }
        break;

    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// 入口
// ============================================================================

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    HWND hWnd;
    RECT rc;
    int w, h, sw, sh;
    MSG msg;
    HRESULT hrCom;

    (void)hPrevInstance;
    (void)lpCmdLine;

    hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

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
        MessageBoxW(NULL, L"注册窗口类失败", L"错误", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    hWnd = CreateWindowExW(
        0, APP_CLASS, APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 400,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBoxW(NULL, L"创建窗口失败", L"错误", MB_OK | MB_ICONERROR);
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

    if (SUCCEEDED(hrCom)) CoUninitialize();
    return (int)msg.wParam;
}