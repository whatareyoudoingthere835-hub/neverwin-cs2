#pragma once
// ============================================================================
// Общее состояние DLL <-> внешний оверлей (shared memory).
//
// neverwin.dll пишет снапшот (фичи, диагностика) каждый тик; оверлей читает
// и рисует меню/HUD. Канал двусторонний: оверлей шлёт команды (тогглы фич,
// выгрузка), DLL их применяет.
//
// Протокол:
//   DLL -> оверлей: поля снапшота + инкремент seq после записи.
//   Оверлей -> DLL: setMask/setValues, затем инкремент cmdSeq; DLL применяет
//   команду и пишет appliedSeq = cmdSeq (ack).
// Инкременты — через Interlocked: они же барьеры видимости для соседних полей.
// ============================================================================
#include <cstdint>
#include <cstring>

#ifndef _WIN32
#error "windows only"
#endif
#include <windows.h>

namespace nwshared {

    constexpr wchar_t  kMapName[] = L"Local\\neverwin_state_v5";
    constexpr uint32_t kMapSize   = 512;
    constexpr uint32_t kMagic     = 0x4E573035; // "NW05"

    // Биты фич в командах.
    enum FeatureBit : uint32_t {
        kFbAntiAimbot   = 1u << 0, // F1 — реверс аимбот
        kFbAntiAimless  = 1u << 1, // F2 — взгляд в пол
        kFbVisualRecoil = 1u << 2, // F3 — отдача x4
        kFbAntiBhop     = 1u << 3, // F4
        kFbGamesense    = 1u << 4, // F5
        kFbUnload       = 1u << 5, // команда: выгрузить DLL
    };

    struct State {
        // --- DLL -> оверлей ---
        uint32_t magic;
        uint32_t seq;             // инкремент после каждого снапшота
        uint32_t ownerPid;
        uint32_t dllVersion;
        uint8_t  antiAimbot;
        uint8_t  antiAimless;
        uint8_t  visualRecoil;
        uint8_t  antiBhop;
        uint8_t  gamesense;
        uint8_t  hudVisible;      // F6
        uint8_t  menuOpen;        // P / INSERT
        uint8_t  inGameMenu;      // 1 = в игру встал рендер-хук (меню рисует DLL)
        uint8_t  unloadRequested; // END / кнопка в меню
        uint64_t clientBase;
        uint64_t entityList;
        uint64_t localPlayer;
        int32_t  localHealth;
        int32_t  localTeam;
        uint8_t  viewAnglesWritable;
        uint8_t  offsetsFromIni;
        uint8_t  pad[6];

        // --- оверлей -> DLL (команды) ---
        uint32_t cmdSeq;
        uint32_t appliedSeq;
        uint32_t setMask;
        uint32_t setValues;
    };
    static_assert(sizeof(State) <= kMapSize, "State не влезает в kMapSize");

    // Владелец (DLL): создаёт маппинг, пишет снапшоты, применяет команды.
    class Publisher {
        HANDLE m_map = nullptr;
        State* m_st  = nullptr;

    public:
        Publisher() {
            m_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, kMapSize, kMapName);
            if (!m_map) {
                // Повторный инжект: маппинг уже создан — открываем существующий.
                m_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMapName);
            }
            if (m_map)
                m_st = static_cast<State*>(MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, kMapSize));
            if (m_st) {
                RtlZeroMemory(m_st, sizeof(State));
                m_st->magic      = kMagic;
                m_st->ownerPid   = GetCurrentProcessId();
                m_st->hudVisible = 1;
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

    // Читатель (оверлей): открывает чужой маппинг, шлёт команды.
    class Viewer {
        HANDLE       m_map = nullptr;
        State*       m_st  = nullptr; // пишем команды — нужен доступ на запись

    public:
        Viewer() = default;
        ~Viewer() { Detach(); }
        Viewer(const Viewer&) = delete;
        Viewer& operator=(const Viewer&) = delete;

        bool Attach() {
            Detach();
            m_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMapName);
            if (!m_map)
                return false;
            m_st = static_cast<State*>(MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, kMapSize));
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
        // Команда DLL: маска битов + их значения. cmdSeq инкрементим последним —
        // DLL по нему видит, что setMask/setValues уже зафиксированы.
        uint32_t SendCommand(uint32_t mask, uint32_t values) {
            if (!Alive())
                return 0;
            m_st->setMask   = mask;
            m_st->setValues = values;
            return static_cast<uint32_t>(
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(&m_st->cmdSeq)));
        }
        bool IsApplied(uint32_t cmdSeq) const {
            return m_st && m_st->appliedSeq == cmdSeq;
        }
    };

} // namespace nwshared
