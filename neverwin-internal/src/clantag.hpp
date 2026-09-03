#pragma once
#include "pch.h"
#include "log.hpp"

// Lightweight name animator adapted from Quint's engine-command approach.
// It uses only public CreateInterface entry points and restores the captured
// base name when disabled.
namespace clantag {
    namespace detail {
        struct CvarEntry { void* cvar; uint16_t prev; uint16_t next; };
        struct EngineCvar { uint8_t pad[80]; CvarEntry* entries; };
        struct Convar {
            const char* name;
            void* next;
            uint8_t pad0[0x10];
            const char* description;
            uint32_t type;
            uint32_t registered;
            uint32_t flags;
            uint8_t pad1[0x24];
            const char* stringValue;
        };

        // UTF-8 -> UTF-16 (для логов: ник с кириллицей/эмодзи в wchar-лог
        // идёт только через правильное преобразование).
        inline std::wstring ToWide(const std::string& s) {
            if (s.empty())
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                              static_cast<int>(s.size()), nullptr, 0);
            if (n <= 0)
                return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        inline void* Interface(const wchar_t* module, const char* version) {
            const HMODULE mod = GetModuleHandleW(module);
            if (!mod) return nullptr;
            using CreateInterfaceFn = void*(__fastcall*)(const char*, int*);
            const auto fn = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mod, "CreateInterface"));
            return fn ? fn(version, nullptr) : nullptr;
        }

        inline const char* FindNameCvar() {
            auto* cvar = reinterpret_cast<EngineCvar*>(Interface(L"tier0.dll", "VEngineCvar007"));
            if (!cvar || !cvar->entries) return nullptr;
            uint16_t index = 0;
            for (int n = 0; n < 65535; ++n) {
                const auto& entry = cvar->entries[index];
                const auto* var = reinterpret_cast<const Convar*>(entry.cvar);
                if (var && var->name && _stricmp(var->name, "name") == 0)
                    return var->stringValue;
                if (entry.next == UINT16_MAX || entry.next == index) break;
                index = entry.next;
            }
            return nullptr;
        }

        inline bool Execute(const std::string& command) {
            void* engine = Interface(L"engine2.dll", "Source2EngineToClient001");
            if (!engine) return false;
            void** vt = *reinterpret_cast<void***>(engine);
            if (!vt || !vt[40]) return false;
            using ExecuteFn = void(__fastcall*)(void*, const char*);
            reinterpret_cast<ExecuteFn>(vt[40])(engine, command.c_str());
            return true;
        }
    }

    class Animator {
    public:
        void Update(bool enabled) {
            if (!enabled) { Reset(); return; }
            if (!m_captured) {
                const char* name = detail::FindNameCvar();
                if (!name || !*name) {
                    LogOnce(1, L"clantag: cvar 'name' not found; waiting for VEngineCvar007.");
                    return;
                }
                m_baseName = StripTag(name);
                m_captured = !m_baseName.empty();
                if (!m_captured) {
                    LogOnce(2, L"clantag: base nickname is empty.");
                    return;
                }
                // CS2 отклоняет имя длиннее 32 СИМВОЛОВ. Если базовый ник
                // длинный, "[NeverWin] " (11) его не пропустит — берём
                // короткий тег "[NW] " (5). До этого именно поэтому
                // «clantag не работает» у людей с длинным ником.
                m_tag = (m_baseName.size() <= 32 - 11) ? kTagFull : kTagShort;
                m_tagLength = std::strlen(m_tag.c_str());
                m_lastTick = GetTickCount();
                // ВАЖНО: м_baseName — UTF-8 (char*). Раньше сюда уходило %S
                // (wide-формат) с char* аргументом: байты UTF-8 (в т.ч. 4
                // байта эмодзи) переинтерпретировались как UTF-16 — отсюда
                // «ромб с вопросом» в логе/DebugView. Теперь переводим.
                LogOnce(3, L"clantag: captured base nickname '%s' (tag '%s').",
                        detail::ToWide(m_baseName).c_str(),
                        detail::ToWide(m_tag).c_str());
            }

            const DWORD now = GetTickCount();
            constexpr DWORD kFrameMs = 250;
            constexpr DWORD kFullHoldMs = 3000;
            if (m_full && now - m_lastTick < kFullHoldMs) return;
            if (!m_full && now - m_lastTick < kFrameMs) return;
            m_lastTick = now;

            if (m_full) { m_full = false; m_index = m_tagLength - 1; }
            else if (m_growing) {
                if (++m_index >= m_tagLength) { m_index = m_tagLength; m_full = true; m_growing = false; }
            } else if (m_index == 0) {
                m_growing = true;
            } else {
                --m_index;
            }
            Apply();
        }

        void Reset() {
            if (!m_captured) return;
            detail::Execute("setinfo name \"" + m_baseName + "\"");
            m_captured = false; m_lastApplied.clear(); m_index = 0; m_growing = true; m_full = false;
        }

    private:
        static constexpr const char* kTagFull  = "[NeverWin]";
        static constexpr const char* kTagShort = "[NW]";
        static std::string StripTag(const std::string& name) {
            const char* tags[2] = { kTagFull, kTagShort };
            for (const char* tag : tags) {
                if (name.rfind(tag, 0) == 0) {
                    size_t p = std::strlen(tag);
                    while (p < name.size() && name[p] == ' ') ++p;
                    return name.substr(p);
                }
            }
            return name;
        }
        void Apply() {
            const std::string tag(m_tag, m_index);
            std::string display;
            if (tag.empty())
                display = m_baseName;
            else
                display = tag + " " + m_baseName;
            // Страховка по лимиту 32 символа (режем по границе UTF-8).
            if (display.size() > 32) {
                size_t cut = 32;
                while (cut > 0 && (static_cast<unsigned char>(display[cut] & 0xC0) == 0x80))
                    --cut;
                display.resize(cut);
            }
            if (display == m_lastApplied) return;
            if (detail::Execute("setinfo name \"" + display + "\"")) {
                m_lastApplied = display;
                LogOnce(4, L"clantag: engine command path is active.");
            } else {
                LogOnce(5, L"clantag: Source2EngineToClient001 / ExecuteClientCmd unavailable.");
            }
        }
        void LogOnce(int code, const wchar_t* fmt, ...) {
            if (m_lastStatus == code) return;
            m_lastStatus = code;
            wchar_t text[256]{};
            va_list args; va_start(args, fmt);
            _vsnwprintf(text, 255, fmt, args);
            va_end(args);
            NW_LOG(L"%s", text);
        }

        std::string m_baseName, m_lastApplied, m_tag;
        size_t m_tagLength = 0;
        DWORD m_lastTick = 0;
        size_t m_index = 0;
        bool m_captured = false, m_growing = true, m_full = false;
        int m_lastStatus = 0;
    };

    inline Animator g_animator;
} // namespace clantag
