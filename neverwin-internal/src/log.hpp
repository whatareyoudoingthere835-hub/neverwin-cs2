#pragma once
#include "pch.h"
#include "util.hpp"

// ============================================================================
// Минимальный лог: пишет в %TEMP%\neverwin.log и в OutputDebugString
// (видно в DebugView). В оригинале не было ни строчки лога — креш на
// инжекте приходилось лечить вслепую.
// ============================================================================
namespace log {

    inline std::wstring GetLogPath() {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        return std::wstring(tmp) + L"neverwin.log";
    }

    inline void Init() {
        // Первая запись: отбивка сессии, чтобы логи не сливались.
        const std::wstring path = GetLogPath();
        FILE* f = _wfopen(path.c_str(), L"ab");
        if (f) {
            const char sep[] = "\n===== neverwin session start =====\n";
            fwrite(sep, 1, sizeof(sep) - 1, f);
            fclose(f);
        }
    }

    inline void Write(const wchar_t* fmt, ...) {
        wchar_t buf[1024];
        va_list args;
        va_start(args, fmt);
        _vsnwprintf(buf, 1023, fmt, args);
        buf[1023] = L'\0';
        va_end(args);

        OutputDebugStringW(buf);
        OutputDebugStringW(L"\n");

        FILE* f = _wfopen(GetLogPath().c_str(), L"ab");
        if (!f)
            return;

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char header[64];
        snprintf(header, sizeof(header), "[%02u:%02u:%02u.%03u] ",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        fwrite(header, 1, strlen(header), f);

        const std::string line = util::ToUtf8(buf);
        fwrite(line.data(), 1, line.size(), f);
        fwrite("\n", 1, 1, f);
        fclose(f);
    }
}

// Удобный макрос с префиксом.
#define NW_LOG(fmt, ...) log::Write(L"[neverwin] " fmt L"\n", ##__VA_ARGS__)
