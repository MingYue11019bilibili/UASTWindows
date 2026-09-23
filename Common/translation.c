/*
 * translation.c - 多语言翻译模块实现
 */

#include "translation.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { LPCWSTR key; LPCWSTR value; } KeyValue;

/* ============================================================================
 * 内置中文默认表
 * ============================================================================ */

static const KeyValue g_defaultTable[] = {
    /* GUI - 主窗口 */
    { L"gui.title",             L"UASTWindows - 图形化提权工具" },
    { L"gui.target",            L"目标程序:" },
    { L"gui.browse",            L"浏览..." },
    { L"gui.user",              L"运行身份:" },
    { L"gui.user.admin",        L"管理员" },
    { L"gui.user.system",       L"SYSTEM" },
    { L"gui.user.ti",           L"TrustedInstaller" },
    { L"gui.priv",              L"特权:" },
    { L"gui.priv.default",      L"默认" },
    { L"gui.priv.enable",       L"启用全部" },
    { L"gui.priv.disable",      L"禁用全部" },
    { L"gui.integ",             L"完整性:" },
    { L"gui.integ.default",     L"默认" },
    { L"gui.integ.system",      L"System" },
    { L"gui.integ.high",        L"High" },
    { L"gui.integ.medium",      L"Medium" },
    { L"gui.integ.low",         L"Low" },
    { L"gui.prio",              L"优先级:" },
    { L"gui.prio.default",      L"默认" },
    { L"gui.prio.realtime",     L"实时" },
    { L"gui.prio.high",         L"高" },
    { L"gui.prio.above",        L"高于正常" },
    { L"gui.prio.normal",       L"正常" },
    { L"gui.prio.below",        L"低于正常" },
    { L"gui.prio.idle",         L"低" },
    { L"gui.window",            L"窗口模式:" },
    { L"gui.window.show",       L"显示" },
    { L"gui.window.hide",       L"隐藏" },
    { L"gui.window.max",        L"最大化" },
    { L"gui.window.min",        L"最小化" },
    { L"gui.wait",              L"等待进程结束" },
    { L"gui.language",          L"语言:" },
    { L"gui.status.ready",      L"就绪" },
    { L"gui.status.calling",    L"已调用 UASTCmdup, 请查看引擎窗口" },
    { L"gui.status.failed",     L"调用 UASTCmdup 失败" },
    { L"gui.btn.run",           L"执行提权" },
    { L"gui.btn.help",          L"帮助" },
    { L"gui.btn.close",         L"关闭" },
    { L"gui.btn.exit",          L"退出程序" },
    { L"gui.help.title",        L"帮助" },
    { L"gui.help.text",
      L"UASTWindows - 图形化提权工具\r\n"
      L"\r\n"
      L"本程序是 UASTCmdup.exe 的图形前端。\r\n"
      L"所有提权操作由 UASTCmdup.exe 完成。\r\n"
      L"\r\n"
      L"命令行用法 (直接调用 UASTCmdup.exe):\r\n"
      L"  UASTCmdup.exe [选项] 命令行\r\n"
      L"  UASTCmdup.exe -language /?        列出所有可用语言\r\n"
      L"  UASTCmdup.exe -language /<代号>   切换语言\r\n" },
    { L"gui.err.no_target",         L"请先指定目标程序" },
    { L"gui.err.no_target.title",   L"提示" },
    { L"gui.err.engine_not_found",
      L"无法启动 UASTCmdup.exe\n"
      L"请确保它与 UASTWindows.exe 位于同一目录。" },
    { L"gui.err.title",             L"错误" },
    { L"gui.lang.restart",          L"语言已切换, 重启程序后生效。" },

    /* CLI */
    { L"cli.console.title",         L"UASTCmdup - 日志" },
    { L"cli.banner",                L"  UASTCmdup %s" },
    { L"cli.self_path",             L"[*] 自身路径: %s" },
    { L"cli.elevate",               L"[*] 当前是普通用户, 尝试提权至管理员..." },
    { L"cli.try.fodhelper",         L"[*] 尝试 fodhelper.exe 劫持..." },
    { L"cli.try.computerdefaults",  L"\n[*] fodhelper 失败, 尝试 computerdefaults.exe..." },
    { L"cli.try.runas",             L"\n[*] 协议劫持均失败, 使用标准 UAC 提权..." },
    { L"cli.reg_written",           L"    [*] 注册表已写入" },
    { L"cli.event",                 L"    [*] 事件名: %s" },
    { L"cli.cmdline",               L"    [*] 命令行: %s" },
    { L"cli.started",               L"    [*] %s 已启动" },
    { L"cli.waiting",               L"    [*] 等待新进程通知 (最多 %d 秒)..." },
    { L"cli.reg_cleaned",           L"    [*] 注册表已清理" },
    { L"cli.notified",              L"    [+] 收到新进程通知, 提权成功" },
    { L"cli.timeout",               L"    [!] 未收到新进程通知, 提权失败 (等待超时)" },
    { L"cli.start_fail",            L"    [!] 启动 %s 失败, 错误码 %lu" },
    { L"cli.reg_fail",              L"    [!] RegCreateKeyEx 失败, 错误码 %ld" },
    { L"cli.runas_fail",            L"    [!] ShellExecute runas 失败, 错误码 %lu" },
    { L"cli.runas_triggered",       L"    [*] 已触发标准 UAC 提权" },
    { L"cli.already_admin",         L"[+] 当前已是管理员" },
    { L"cli.get_token",             L"[*] 获取目标令牌 (模式 %d)..." },
    { L"cli.token_got",             L"[+] 已获得目标令牌" },
    { L"cli.create_proc",           L"[*] 创建子进程..." },
    { L"cli.proc_started",          L"[+] 子进程已启动, PID: %lu" },
    { L"cli.done",                  L"[+] 完成" },
    { L"cli.triggered",             L"\n[+] 已触发提权, 请在新进程中查看结果" },
    { L"cli.all_failed",            L"\n[!] 所有提权方式均失败" },
    { L"cli.no_target",             L"[!] 未指定目标命令行" },
    { L"cli.use_help",              L"[*] 使用 -Help 查看用法" },
    { L"cli.err_unknown_opt",       L"[!] 参数错误: %s" },

    /* 语言命令 */
    { L"cli.lang.help",
      L"用法:\r\n"
      L"  UASTCmdup.exe -language /?         列出所有可用语言\r\n"
      L"  UASTCmdup.exe -language /<代号>    切换语言并保存\r\n"
      L"\r\n"
      L"示例:\r\n"
      L"  UASTCmdup.exe -language /zh_cn     切换到简体中文\r\n"
      L"  UASTCmdup.exe -language /en_us     切换到英语\r\n" },
    { L"cli.lang.list_header",      L"可用语言列表:" },
    { L"cli.lang.current_mark",     L"  当前语言: %s" },
    { L"cli.lang.switched",         L"[+] 语言已切换为: %s, 重启程序后生效" },
    { L"cli.lang.not_found",        L"[!] 未找到语言: %s" },
    { L"cli.lang.use_help",         L"[*] 使用 -language /? 查看可用语言" },

    { NULL, NULL }
};

/* ============================================================================
 * 内置英文默认表 (首次运行自动生成 language.json 时使用)
 * ============================================================================ */

static const KeyValue g_defaultTableEn[] = {
    { L"gui.title",             L"UASTWindows - GUI Elevation Tool" },
    { L"gui.target",            L"Target:" },
    { L"gui.browse",            L"Browse..." },
    { L"gui.user",              L"Run as:" },
    { L"gui.user.admin",        L"Administrator" },
    { L"gui.user.system",       L"SYSTEM" },
    { L"gui.user.ti",           L"TrustedInstaller" },
    { L"gui.priv",              L"Privileges:" },
    { L"gui.priv.default",      L"Default" },
    { L"gui.priv.enable",       L"Enable all" },
    { L"gui.priv.disable",      L"Disable all" },
    { L"gui.integ",             L"Integrity:" },
    { L"gui.integ.default",     L"Default" },
    { L"gui.integ.system",      L"System" },
    { L"gui.integ.high",        L"High" },
    { L"gui.integ.medium",      L"Medium" },
    { L"gui.integ.low",         L"Low" },
    { L"gui.prio",              L"Priority:" },
    { L"gui.prio.default",      L"Default" },
    { L"gui.prio.realtime",     L"Realtime" },
    { L"gui.prio.high",         L"High" },
    { L"gui.prio.above",        L"Above Normal" },
    { L"gui.prio.normal",       L"Normal" },
    { L"gui.prio.below",        L"Below Normal" },
    { L"gui.prio.idle",         L"Idle" },
    { L"gui.window",            L"Window Mode:" },
    { L"gui.window.show",       L"Show" },
    { L"gui.window.hide",       L"Hide" },
    { L"gui.window.max",        L"Maximize" },
    { L"gui.window.min",        L"Minimize" },
    { L"gui.wait",              L"Wait for exit" },
    { L"gui.language",          L"Language:" },
    { L"gui.status.ready",      L"Ready" },
    { L"gui.status.calling",    L"UASTCmdup launched" },
    { L"gui.status.failed",     L"Failed to launch UASTCmdup" },
    { L"gui.btn.run",           L"Run" },
    { L"gui.btn.help",          L"Help" },
    { L"gui.btn.close",         L"Close" },
    { L"gui.btn.exit",          L"Exit" },
    { L"gui.help.title",        L"Help" },
    { L"gui.help.text",
      L"UASTWindows - GUI Elevation Tool\r\n"
      L"\r\n"
      L"This is the GUI frontend for UASTCmdup.exe.\r\n"
      L"All elevation is performed by UASTCmdup.exe.\r\n"
      L"\r\n"
      L"Command line usage (call UASTCmdup.exe directly):\r\n"
      L"  UASTCmdup.exe [options] command\r\n"
      L"  UASTCmdup.exe -language /?        List available languages\r\n"
      L"  UASTCmdup.exe -language /<code>   Switch language\r\n" },
    { L"gui.err.no_target",         L"Please specify a target program" },
    { L"gui.err.no_target.title",   L"Notice" },
    { L"gui.err.engine_not_found",
      L"Cannot launch UASTCmdup.exe.\n"
      L"Make sure it is in the same folder as UASTWindows.exe." },
    { L"gui.err.title",             L"Error" },
    { L"gui.lang.restart",          L"Language changed. Restart to apply." },

    { L"cli.console.title",         L"UASTCmdup - Log" },
    { L"cli.banner",                L"  UASTCmdup %s" },
    { L"cli.self_path",             L"[*] Self path: %s" },
    { L"cli.elevate",               L"[*] Normal user, attempting elevation..." },
    { L"cli.try.fodhelper",         L"[*] Trying fodhelper.exe hijack..." },
    { L"cli.try.computerdefaults",  L"\n[*] fodhelper failed, trying computerdefaults.exe..." },
    { L"cli.try.runas",             L"\n[*] Both hijack methods failed, using standard UAC..." },
    { L"cli.reg_written",           L"    [*] Registry written" },
    { L"cli.event",                 L"    [*] Event: %s" },
    { L"cli.cmdline",               L"    [*] Command line: %s" },
    { L"cli.started",               L"    [*] %s started" },
    { L"cli.waiting",               L"    [*] Waiting for new process (up to %d seconds)..." },
    { L"cli.reg_cleaned",           L"    [*] Registry cleaned" },
    { L"cli.notified",              L"    [+] New process notified, elevation succeeded" },
    { L"cli.timeout",               L"    [!] No notification received, elevation failed (timeout)" },
    { L"cli.start_fail",            L"    [!] Failed to start %s, error %lu" },
    { L"cli.reg_fail",              L"    [!] RegCreateKeyEx failed, error %ld" },
    { L"cli.runas_fail",            L"    [!] ShellExecute runas failed, error %lu" },
    { L"cli.runas_triggered",       L"    [*] Standard UAC triggered" },
    { L"cli.already_admin",         L"[+] Already admin" },
    { L"cli.get_token",             L"[*] Getting token (mode %d)..." },
    { L"cli.token_got",             L"[+] Token acquired" },
    { L"cli.create_proc",           L"[*] Creating subprocess..." },
    { L"cli.proc_started",          L"[+] Subprocess started, PID: %lu" },
    { L"cli.done",                  L"[+] Done" },
    { L"cli.triggered",             L"\n[+] Elevation triggered, check the new process" },
    { L"cli.all_failed",            L"\n[!] All elevation methods failed" },
    { L"cli.no_target",             L"[!] No target command specified" },
    { L"cli.use_help",              L"[*] Use -Help for usage" },
    { L"cli.err_unknown_opt",       L"[!] Parameter error: %s" },

    { L"cli.lang.help",
      L"Usage:\r\n"
      L"  UASTCmdup.exe -language /?         List available languages\r\n"
      L"  UASTCmdup.exe -language /<code>    Switch language and save\r\n"
      L"\r\n"
      L"Examples:\r\n"
      L"  UASTCmdup.exe -language /zh_cn     Switch to Simplified Chinese\r\n"
      L"  UASTCmdup.exe -language /en_us     Switch to English\r\n" },
    { L"cli.lang.list_header",      L"Available languages:" },
    { L"cli.lang.current_mark",     L"  Current: %s" },
    { L"cli.lang.switched",         L"[+] Language switched to: %s, restart to apply" },
    { L"cli.lang.not_found",        L"[!] Language not found: %s" },
    { L"cli.lang.use_help",         L"[*] Use -language /? to list available languages" },

    { NULL, NULL }
};

/* ============================================================================
 * 内部数据结构
 * ============================================================================ */

typedef struct {
    WCHAR   code[32];
    WCHAR   name[64];
    WCHAR** keys;
    WCHAR** values;
    int     count;
    int     capacity;
} LangEntry;

static WCHAR      g_jsonPath[MAX_PATH] = { 0 };
static WCHAR      g_current[32] = L"zh_cn";
static LangEntry* g_langs = NULL;
static int        g_langCount = 0;
static int        g_langCapacity = 0;

/* ============================================================================
 * 内部工具
 * ============================================================================ */

static WCHAR* DupW(const WCHAR* s) {
    if (!s) return NULL;
    size_t len = wcslen(s) + 1;
    WCHAR* p = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (p) wcscpy_s(p, len, s);
    return p;
}

static LangEntry* GetLangByCode(LPCWSTR code) {
    for (int i = 0; i < g_langCount; i++)
        if (_wcsicmp(g_langs[i].code, code) == 0) return &g_langs[i];
    return NULL;
}

static LPCWSTR FindBuiltin(LPCWSTR key) {
    for (int i = 0; g_defaultTable[i].key; i++)
        if (wcscmp(g_defaultTable[i].key, key) == 0)
            return g_defaultTable[i].value;
    return NULL;
}

static void LangSet(LangEntry* lang, LPCWSTR key, LPCWSTR value) {
    for (int i = 0; i < lang->count; i++) {
        if (wcscmp(lang->keys[i], key) == 0) {
            free(lang->values[i]);
            lang->values[i] = DupW(value);
            return;
        }
    }
    if (lang->count >= lang->capacity) {
        int ncap = lang->capacity ? lang->capacity * 2 : 32;
        lang->keys = (WCHAR**)realloc(lang->keys, ncap * sizeof(WCHAR*));
        lang->values = (WCHAR**)realloc(lang->values, ncap * sizeof(WCHAR*));
        lang->capacity = ncap;
    }
    lang->keys[lang->count] = DupW(key);
    lang->values[lang->count] = DupW(value);
    lang->count++;
}

static LangEntry* AddLang(LPCWSTR code, LPCWSTR name) {
    if (g_langCount >= g_langCapacity) {
        int ncap = g_langCapacity ? g_langCapacity * 2 : 4;
        g_langs = (LangEntry*)realloc(g_langs, ncap * sizeof(LangEntry));
        g_langCapacity = ncap;
    }
    LangEntry* le = &g_langs[g_langCount++];
    memset(le, 0, sizeof(LangEntry));
    wcsncpy_s(le->code, 32, code, _TRUNCATE);
    wcsncpy_s(le->name, 64, name, _TRUNCATE);
    return le;
}

static void LoadTableInto(LangEntry* le, const KeyValue* table) {
    for (int i = 0; table[i].key; i++)
        LangSet(le, table[i].key, table[i].value);
}

static void LoadBuiltinInto(LangEntry* le) {
    LoadTableInto(le, g_defaultTable);
}

static void LoadEnglishInto(LangEntry* le) {
    LoadTableInto(le, g_defaultTableEn);
}

static void LoadStringsFromJson(LangEntry* le, JsonValue* stringsObj) {
    if (!stringsObj || stringsObj->type != JSON_OBJECT) return;
    int n = JsonCount(stringsObj);
    for (int i = 0; i < n; i++) {
        LPCWSTR key = JsonKeyAt(stringsObj, i);
        JsonValue* v = JsonValueAt(stringsObj, i);
        if (v && v->type == JSON_STRING) LangSet(le, key, v->u.string);
    }
}

static void FreeAllLangs(void) {
    if (!g_langs) return;
    for (int i = 0; i < g_langCount; i++) {
        for (int k = 0; k < g_langs[i].count; k++) {
            free(g_langs[i].keys[k]);
            free(g_langs[i].values[k]);
        }
        free(g_langs[i].keys);
        free(g_langs[i].values);
    }
    free(g_langs);
    g_langs = NULL;
    g_langCount = 0;
    g_langCapacity = 0;
}

/* ============================================================================
 * 保存 (供 TrInit 内部与 TrSetLanguage 使用)
 * ============================================================================ */

static void WriteJsonString(FILE* fp, const WCHAR* s) {
    fputc('"', fp);
    for (; *s; s++) {
        WCHAR c = *s;
        switch (c) {
        case L'"':  fputs("\\\"", fp); break;
        case L'\\': fputs("\\\\", fp); break;
        case L'\n': fputs("\\n", fp); break;
        case L'\r': fputs("\\r", fp); break;
        case L'\t': fputs("\\t", fp); break;
        default: {
            char buf[8] = { 0 };
            int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, buf, sizeof(buf) - 1, NULL, NULL);
            if (n > 0) fwrite(buf, 1, n, fp);
            break;
        }
        }
    }
    fputc('"', fp);
}

BOOL TrSave(void) {
    FILE* fp = NULL;
    if (!g_jsonPath[0]) return FALSE;
    if (_wfopen_s(&fp, g_jsonPath, L"wb") != 0 || !fp) return FALSE;

    fputs("\xEF\xBB\xBF", fp);   /* UTF-8 BOM */
    fputs("{\n  \"current\": ", fp);
    WriteJsonString(fp, g_current);
    fputs(",\n  \"languages\": {\n", fp);

    for (int i = 0; i < g_langCount; i++) {
        LangEntry* le = &g_langs[i];
        fputs("    ", fp);
        WriteJsonString(fp, le->code);
        fputs(": {\n      \"name\": ", fp);
        WriteJsonString(fp, le->name);
        fputs(",\n      \"strings\": {\n", fp);
        for (int k = 0; k < le->count; k++) {
            fputs("        ", fp);
            WriteJsonString(fp, le->keys[k]);
            fputs(": ", fp);
            WriteJsonString(fp, le->values[k]);
            if (k + 1 < le->count) fputs(",", fp);
            fputs("\n", fp);
        }
        fputs("      }\n    }", fp);
        if (i + 1 < g_langCount) fputs(",", fp);
        fputs("\n", fp);
    }
    fputs("  }\n}\n", fp);
    fclose(fp);
    return TRUE;
}

/* ============================================================================
 * 接口
 * ============================================================================ */

BOOL TrInit(LPCWSTR jsonPath) {
    FreeAllLangs();

    if (jsonPath) wcsncpy_s(g_jsonPath, MAX_PATH, jsonPath, _TRUNCATE);

    /* ---- 先建立内置中文作为兜底 ---- */
    LangEntry* zh = AddLang(L"zh_cn", L"简体中文");
    LoadBuiltinInto(zh);
    wcsncpy_s(g_current, 32, L"zh_cn", _TRUNCATE);

    if (!jsonPath) return FALSE;

    /* ---- 尝试加载 json ---- */
    JsonValue* root = JsonParseFile(jsonPath);

    /* ★ 文件不存在或损坏: 建立默认双语表, 并自动生成文件 */
    if (!root) {
        LangEntry* en = AddLang(L"en_us", L"English");
        LoadEnglishInto(en);
        TrSave();          /* 立即写盘, 生成 language.json */
        return FALSE;      /* 返回 FALSE 表示"未从文件加载" */
    }

    /* ---- 读取 current ---- */
    const WCHAR* cur = JsonGetString(root, L"current", L"zh_cn");
    if (cur) wcsncpy_s(g_current, 32, cur, _TRUNCATE);

    /* ---- 读取 languages 节点 ---- */
    JsonValue* langs = JsonGet(root, L"languages");
    if (langs && langs->type == JSON_OBJECT) {
        int n = JsonCount(langs);
        for (int i = 0; i < n; i++) {
            LPCWSTR code = JsonKeyAt(langs, i);
            JsonValue* lv = JsonValueAt(langs, i);
            if (!lv || lv->type != JSON_OBJECT) continue;

            const WCHAR* name = JsonGetString(lv, L"name", code);
            LangEntry* le = GetLangByCode(code);
            if (!le) le = AddLang(code, name);
            else wcsncpy_s(le->name, 64, name, _TRUNCATE);

            LoadStringsFromJson(le, JsonGet(lv, L"strings"));
        }
    }

    JsonFree(root);
    return TRUE;
}

void TrShutdown(void) { FreeAllLangs(); }

const WCHAR* Tr(LPCWSTR key) {
    LangEntry* le = GetLangByCode(g_current);
    if (le) {
        for (int i = 0; i < le->count; i++)
            if (wcscmp(le->keys[i], key) == 0) return le->values[i];
    }
    const WCHAR* d = FindBuiltin(key);
    return d ? d : key;
}

int TrLanguageCount(void) { return g_langCount; }

LPCWSTR TrLanguageCode(int idx) {
    if (idx < 0 || idx >= g_langCount) return NULL;
    return g_langs[idx].code;
}

LPCWSTR TrLanguageName(int idx) {
    if (idx < 0 || idx >= g_langCount) return NULL;
    return g_langs[idx].name;
}

LPCWSTR TrCurrentCode(void) { return g_current; }

BOOL TrSetLanguage(LPCWSTR code) {
    LangEntry* le = GetLangByCode(code);
    if (!le) return FALSE;
    wcsncpy_s(g_current, 32, le->code, _TRUNCATE);
    TrSave();
    return TRUE;
}