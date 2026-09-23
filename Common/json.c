/*
 * json.c - 精简 JSON 解析器实现
 */

#include "json.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const WCHAR* text; int pos; int len; } JsonParser;

static JsonValue* ParseValue(JsonParser* p);

static JsonValue* NewValue(JsonType t) {
    JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
    if (v) v->type = t;
    return v;
}

static void SkipWS(JsonParser* p) {
    while (p->pos < p->len) {
        WCHAR c = p->text[p->pos];
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') p->pos++;
        else break;
    }
}

static WCHAR* ParseString(JsonParser* p) {
    int out, cap;
    WCHAR* buf;
    if (p->pos >= p->len || p->text[p->pos] != L'"') return NULL;
    p->pos++;
    cap = 64;
    buf = (WCHAR*)malloc(cap * sizeof(WCHAR));
    if (!buf) return NULL;
    out = 0;
    while (p->pos < p->len) {
        WCHAR c = p->text[p->pos++];
        if (c == L'"') { buf[out] = L'\0'; return buf; }
        if (c == L'\\') {
            if (p->pos >= p->len) break;
            c = p->text[p->pos++];
            switch (c) {
            case L'"':  c = L'"';  break;
            case L'\\': c = L'\\'; break;
            case L'/':  c = L'/';  break;
            case L'n':  c = L'\n'; break;
            case L'r':  c = L'\r'; break;
            case L't':  c = L'\t'; break;
            case L'b':  c = L'\b'; break;
            case L'f':  c = L'\f'; break;
            }
        }
        if (out + 1 >= cap) {
            cap *= 2;
            buf = (WCHAR*)realloc(buf, cap * sizeof(WCHAR));
            if (!buf) return NULL;
        }
        buf[out++] = c;
    }
    free(buf);
    return NULL;
}

static JsonValue* ParseObject(JsonParser* p) {
    JsonValue* obj = NewValue(JSON_OBJECT);
    if (!obj) return NULL;
    if (p->pos >= p->len || p->text[p->pos] != L'{') return obj;
    p->pos++;
    for (;;) {
        SkipWS(p);
        if (p->pos >= p->len) break;
        if (p->text[p->pos] == L'}') { p->pos++; break; }
        WCHAR* key = ParseString(p);
        if (!key) break;
        SkipWS(p);
        if (p->pos >= p->len || p->text[p->pos] != L':') { free(key); break; }
        p->pos++;
        JsonValue* val = ParseValue(p);
        if (!val) { free(key); break; }
        if (obj->u.object.count >= obj->u.object.capacity) {
            int ncap = obj->u.object.capacity ? obj->u.object.capacity * 2 : 8;
            obj->u.object.keys = (WCHAR**)realloc(obj->u.object.keys, ncap * sizeof(WCHAR*));
            obj->u.object.values = (JsonValue**)realloc(obj->u.object.values, ncap * sizeof(JsonValue*));
            obj->u.object.capacity = ncap;
        }
        obj->u.object.keys[obj->u.object.count] = key;
        obj->u.object.values[obj->u.object.count] = val;
        obj->u.object.count++;
        SkipWS(p);
        if (p->pos < p->len && p->text[p->pos] == L',') { p->pos++; continue; }
        if (p->pos < p->len && p->text[p->pos] == L'}') { p->pos++; break; }
    }
    return obj;
}

static JsonValue* ParseArray(JsonParser* p) {
    JsonValue* arr = NewValue(JSON_ARRAY);
    if (!arr) return NULL;
    if (p->pos >= p->len || p->text[p->pos] != L'[') return arr;
    p->pos++;
    for (;;) {
        SkipWS(p);
        if (p->pos >= p->len) break;
        if (p->text[p->pos] == L']') { p->pos++; break; }
        JsonValue* val = ParseValue(p);
        if (!val) break;
        if (arr->u.array.count >= arr->u.array.capacity) {
            int ncap = arr->u.array.capacity ? arr->u.array.capacity * 2 : 8;
            arr->u.array.items = (JsonValue**)realloc(arr->u.array.items, ncap * sizeof(JsonValue*));
            arr->u.array.capacity = ncap;
        }
        arr->u.array.items[arr->u.array.count++] = val;
        SkipWS(p);
        if (p->pos < p->len && p->text[p->pos] == L',') { p->pos++; continue; }
        if (p->pos < p->len && p->text[p->pos] == L']') { p->pos++; break; }
    }
    return arr;
}

static JsonValue* ParseNumber(JsonParser* p) {
    JsonValue* v = NewValue(JSON_NUMBER);
    WCHAR buf[64];
    int i = 0;
    while (p->pos < p->len && i < 63) {
        WCHAR c = p->text[p->pos];
        if ((c >= L'0' && c <= L'9') || c == L'-' || c == L'+' ||
            c == L'.' || c == L'e' || c == L'E') {
            buf[i++] = c; p->pos++;
        }
        else break;
    }
    buf[i] = L'\0';
    v->u.number = _wtof(buf);
    return v;
}

static JsonValue* ParseValue(JsonParser* p) {
    SkipWS(p);
    if (p->pos >= p->len) return NULL;
    WCHAR c = p->text[p->pos];
    if (c == L'{') return ParseObject(p);
    if (c == L'[') return ParseArray(p);
    if (c == L'"') {
        WCHAR* s = ParseString(p);
        JsonValue* v = NewValue(JSON_STRING);
        v->u.string = s ? s : _wcsdup(L"");
        return v;
    }
    if (c == L't' && p->pos + 4 <= p->len && wcsncmp(p->text + p->pos, L"true", 4) == 0) {
        p->pos += 4;
        JsonValue* v = NewValue(JSON_BOOL); v->u.boolean = TRUE; return v;
    }
    if (c == L'f' && p->pos + 5 <= p->len && wcsncmp(p->text + p->pos, L"false", 5) == 0) {
        p->pos += 5;
        JsonValue* v = NewValue(JSON_BOOL); v->u.boolean = FALSE; return v;
    }
    if (c == L'n' && p->pos + 4 <= p->len && wcsncmp(p->text + p->pos, L"null", 4) == 0) {
        p->pos += 4;
        return NewValue(JSON_NULL);
    }
    if (c == L'-' || c == L'+' || (c >= L'0' && c <= L'9')) return ParseNumber(p);
    return NULL;
}

void JsonFree(JsonValue* v) {
    if (!v) return;
    switch (v->type) {
    case JSON_STRING: free(v->u.string); break;
    case JSON_ARRAY:
        for (int i = 0; i < v->u.array.count; i++) JsonFree(v->u.array.items[i]);
        free(v->u.array.items);
        break;
    case JSON_OBJECT:
        for (int i = 0; i < v->u.object.count; i++) {
            free(v->u.object.keys[i]);
            JsonFree(v->u.object.values[i]);
        }
        free(v->u.object.keys);
        free(v->u.object.values);
        break;
    }
    free(v);
}

JsonValue* JsonParse(const WCHAR* text) {
    JsonParser p;
    if (!text) return NULL;
    p.text = text; p.pos = 0;
    p.len = (int)wcslen(text);
    return ParseValue(&p);
}

JsonValue* JsonParseFile(LPCWSTR path) {
    HANDLE hFile;
    DWORD size, read = 0;
    char* raw = NULL;
    WCHAR* wide = NULL;
    JsonValue* v = NULL;

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(hFile); return NULL; }

    {
        BYTE bom[3] = { 0 };
        DWORD got = 0;
        ReadFile(hFile, bom, 3, &got, NULL);
        if (got == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
            size -= 3;
        else
            SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    }

    raw = (char*)malloc(size + 1);
    if (!raw) { CloseHandle(hFile); return NULL; }
    if (!ReadFile(hFile, raw, size, &read, NULL)) {
        free(raw); CloseHandle(hFile); return NULL;
    }
    raw[read] = 0;
    CloseHandle(hFile);

    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, raw, -1, NULL, 0);
        if (wlen > 0) {
            wide = (WCHAR*)malloc(wlen * sizeof(WCHAR));
            if (wide) {
                MultiByteToWideChar(CP_UTF8, 0, raw, -1, wide, wlen);
                v = JsonParse(wide);
                free(wide);
            }
        }
    }
    free(raw);
    return v;
}

JsonValue* JsonGet(JsonValue* obj, LPCWSTR key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (int i = 0; i < obj->u.object.count; i++)
        if (wcscmp(obj->u.object.keys[i], key) == 0)
            return obj->u.object.values[i];
    return NULL;
}

const WCHAR* JsonGetString(JsonValue* obj, LPCWSTR key, LPCWSTR def) {
    JsonValue* v = JsonGet(obj, key);
    if (!v || v->type != JSON_STRING) return def;
    return v->u.string;
}

int JsonCount(JsonValue* obj) {
    if (!obj || obj->type != JSON_OBJECT) return 0;
    return obj->u.object.count;
}

LPCWSTR JsonKeyAt(JsonValue* obj, int i) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    if (i < 0 || i >= obj->u.object.count) return NULL;
    return obj->u.object.keys[i];
}

JsonValue* JsonValueAt(JsonValue* obj, int i) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    if (i < 0 || i >= obj->u.object.count) return NULL;
    return obj->u.object.values[i];
}