/*
 * translation.h - 多语言翻译模块
 */

#pragma once
#include <windows.h>

BOOL         TrInit(LPCWSTR jsonPath);
void         TrShutdown(void);
const WCHAR* Tr(LPCWSTR key);
BOOL         TrSetLanguage(LPCWSTR code);
BOOL         TrSave(void);

int          TrLanguageCount(void);
LPCWSTR      TrLanguageCode(int idx);
LPCWSTR      TrLanguageName(int idx);
LPCWSTR      TrCurrentCode(void);