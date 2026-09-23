/*
 * json.h - 精简 JSON 解析器
 *
 * 支持: null / bool / number / string / array / object
 * 字符串支持 \" \\ \/ \n \r \t \b \f 转义, 不支持 \uXXXX
 * 编码: WCHAR (UTF-16)
 * 仅用于读取, 不支持序列化。
 */

#pragma once
#include <windows.h>

typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct _JsonValue JsonValue;

struct _JsonValue {
    JsonType type;
    union {
        BOOL   boolean;
        double number;
        WCHAR* string;
        struct { JsonValue** items; int count; int capacity; } array;
        struct { WCHAR** keys; JsonValue** values; int count; int capacity; } object;
    } u;
};

JsonValue* JsonParseFile(LPCWSTR path);
JsonValue* JsonParse(const WCHAR* text);
void         JsonFree(JsonValue* v);
JsonValue* JsonGet(JsonValue* obj, LPCWSTR key);
const WCHAR* JsonGetString(JsonValue* obj, LPCWSTR key, LPCWSTR def);
int          JsonCount(JsonValue* obj);
LPCWSTR      JsonKeyAt(JsonValue* obj, int i);
JsonValue* JsonValueAt(JsonValue* obj, int i);