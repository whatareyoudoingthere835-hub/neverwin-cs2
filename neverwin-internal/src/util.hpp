#pragma once
#include <string>

namespace util {

    // UTF-16 (wchar_t) -> UTF-8 байты.
    // ImGui (AddFontFromFileTTF) принимает пути только в UTF-8.
    inline std::string ToUtf8(const std::wstring& w) {
        if (w.empty())
            return {};

        const int needed = WideCharToMultiByte(
            CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
            nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            return {};

        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
            out.data(), needed, nullptr, nullptr);
        return out;
    }
}
