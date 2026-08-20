#pragma once
// ============================================================================
// Общее состояние DLL для внешнего оверлея (shared memory).
//
// neverwin.dll пишет сюда снапшот фич и диагностики каждый тик,
// neverwin_overlay.exe читает и рисует HUD. Оверлей внешний: он не лезет
// в игру, поэтому работает при любом рендерере (DX11/Vulkan).
//
// Протокол: Publisher (DLL) — создатель маппинга, пишет поля, фиксирует
// снапшот инкрементом seq. Viewer (оверлей) открывает маппинг, читает seq
// до и после копии — если совпал, снапшот без разрыва.
// ============================================================================
#include <cstdint>
#include <cstring>

#ifndef _WIN32
#error "windows only"
#endif
#include <windows.h>

namespace nwshared {

    constexpr wchar_t  kMapName[] = L"Local\\neverwin_state_v3";
    constexpr uint32_t kMapSize   = 256;
    constexpr uint32_t kMagic     = 0x4E573033; // "NW03"

    struct State {
        uint32_t magic;          // kMagic, иначе маппинг чужой/мёртвый
        uint32_t seq;            // инкрементится после каждой записи
        uint32_t ownerPid;       // PID игры (владелец маппинга)
        uint32_t dllVersion;     // версия сборки DLL (vN), 0 = неизвестно

        // фичи (1 = ON)
        uint8_t antiAimbot;      // F1 — реверс аимбот
        uint8_t antiAimless;     // F2 — взгляд в пол
        uint8_t visualRecoil;    // F3 — отдача x4
        uint8_t antiBhop;        // F4
        uint8_t gamesense;       // F5
        uint8_t hudVisible;      // F6 — показать/скрыть внешний HUD
        uint8_t menuOpen;        // INSERT — встроенное ImGui-меню
        uint8_t unloadRequested; // END — DLL выгружается, оверлею на выход

        // диагностика
        uint64_t clientBase;
        uint64_t entityList;
        uint64_t localPlayer;
        int32_t  localHealth;
        int32_t  localTeam;
        uint8_t  viewAnglesWritable;
        uint8_t  offsetsFromIni;
        uint8_t  pad1[6];
    };
    static_assert(sizeof(State) <= kMapSize, "State не влезает в kMapSize");

    // Владелец (DLL): создаёт маппинг, пишет, инкрементит seq.
    class Publisher {
        HANDLE m_map = nullptr;
        State* m_st  = nullptr;

    public:
        Publisher() {
            m_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, kMapSize, kMapName);
            if (!m_map) {
                // Кто-то уже создал (повторный инжект) — открываем существующий.
                m_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMapName);
            }
            if (m_map)
                m_st = static_cast<State*>(MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, kMapSize));
            if (m_st) {
                RtlZeroMemory(m_st, sizeof(State));
                m_st->magic      = kMagic;
                m_st->ownerPid   = GetCurrentProcessId();
                m_st->hudVisible = 1; // HUD виден сразу после старта оверлея
            }
        }
        ~Publisher() {
            if (m_st)
                UnmapViewOfFile(m_st);
            if (m_map)
                CloseHandle(m_map);
        }
        Publisher(const Publisher&) = delete;
        Publisher& operator=(const Publisher&) = delete;

        State* operator->() { return m_st; }
        explicit operator bool() const { return m_st != nullptr; }

        // Поля уже записаны — фиксируем снапшот инкрементом seq.
        void Commit() {
            if (m_st)
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(&m_st->seq));
        }
    };

    // Читатель (оверлей): открывает чужой маппинг, снимает атомарный снапшот.
    class Viewer {
        HANDLE       m_map = nullptr;
        const State* m_st  = nullptr;

    public:
        Viewer() = default;
        ~Viewer() { Detach(); }
        Viewer(const Viewer&) = delete;
        Viewer& operator=(const Viewer&) = delete;

        bool Attach() {
            Detach();
            m_map = OpenFileMappingW(FILE_MAP_READ, FALSE, kMapName);
            if (!m_map)
                return false;
            m_st = static_cast<const State*>(MapViewOfFile(m_map, FILE_MAP_READ, 0, 0, kMapSize));
            return m_st != nullptr;
        }
        void Detach() {
            if (m_st)
                UnmapViewOfFile(m_st);
            if (m_map)
                CloseHandle(m_map);
            m_st  = nullptr;
            m_map = nullptr;
        }
        bool Alive() const {
            return m_st && m_st->magic == kMagic;
        }
        // Снапшот без разрыва: seq до и после копии должен совпасть.
        bool Snapshot(State& out) const {
            if (!Alive())
                return false;
            for (int attempt = 0; attempt < 3; ++attempt) {
                const uint32_t before = m_st->seq;
                memcpy(&out, m_st, sizeof(State));
                const uint32_t after = m_st->seq;
                if (before == after)
                    return true;
            }
            return false;
        }
    };

} // namespace nwshared
