#pragma once
#include "pch.h"

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
                if (!name || !*name) return;
                m_baseName = StripTag(name);
                m_captured = !m_baseName.empty();
                if (!m_captured) return;
                m_lastTick = GetTickCount();
            }

            const DWORD now = GetTickCount();
            constexpr DWORD kFrameMs = 250;
            constexpr DWORD kFullHoldMs = 3000;
            if (m_full && now - m_lastTick < kFullHoldMs) return;
            if (!m_full && now - m_lastTick < kFrameMs) return;
            m_lastTick = now;

            if (m_full) { m_full = false; m_index = kTagLength - 1; }
            else if (m_growing) {
                if (++m_index >= kTagLength) { m_index = kTagLength; m_full = true; m_growing = false; }
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
        static constexpr const char* kTag = "[NeverWin]";
        static constexpr size_t kTagLength = 10;
        static std::string StripTag(const std::string& name) {
            if (name.rfind(kTag, 0) == 0) {
                size_t p = std::strlen(kTag);
                while (p < name.size() && name[p] == ' ') ++p;
                return name.substr(p);
            }
            return name;
        }
        void Apply() {
            const std::string tag(kTag, m_index);
            const std::string display = tag.empty() ? m_baseName : tag + " " + m_baseName;
            if (display == m_lastApplied) return;
            if (detail::Execute("setinfo name \"" + display + "\"")) m_lastApplied = display;
        }

        std::string m_baseName, m_lastApplied;
        DWORD m_lastTick = 0;
        size_t m_index = 0;
        bool m_captured = false, m_growing = true, m_full = false;
    };

    inline Animator g_animator;
} // namespace clantag
