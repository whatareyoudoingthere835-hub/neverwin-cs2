#pragma once
#include <cstdint>

// ============================================================================
// ОФФСЕТЫ.
//
// Встроенные значения — от последнего известного дампа схем. CS2 обновляется
// почти каждую неделю, поэтому актуальные оффсеты грузятся из neverwin.ini,
// лежащего рядом с DLL (секция [offsets], значения в hex). Если ini нет —
// используются встроенные, и меню честно покажет "ВСТРОЕННЫЕ — обнови!".
//
// Как получить свежий ini после патча Valve — README и tools/dump_to_ini.py.
// ============================================================================
namespace offsets {

    struct Offsets {
        // --- client.dll ---
        std::uintptr_t dwEntityList      = 0x2554050; // CGameEntitySystem::m_list
        std::uintptr_t dwLocalPlayerPawn = 0x23A9118;
        std::uintptr_t dwViewAngles      = 0x23BF1A8;

        // --- схема C_CSPlayerPawnBase ---
        std::uintptr_t m_iHealth         = 0x34C;
        std::uintptr_t m_iTeamNum        = 0x3E7;
        std::uintptr_t m_fFlags          = 0x3F4;  // бит 0 = FL_ONGROUND
        std::uintptr_t m_aimPunchAngle   = 0x14CC; // QAngle (pitch, yaw)
        std::uintptr_t m_pClippingWeapon = 0x1308; // CEntityHandle (u32)
        std::uintptr_t m_iClip1          = 0x15A4;
        std::uintptr_t m_bInReload       = 0x1704;

        // --- позиции, для реверс аимбота (F1) ---
        // Самые нестабильные из схемных: значения от последнего известного
        // дампа. После патча Valve — свежие из dump_to_ini.py, как обычно.
        std::uintptr_t m_pGameSceneNode  = 0x318;  // C_BaseEntity → CGameSceneNode*
        std::uintptr_t m_vecAbsOrigin    = 0xC8;   // CGameSceneNode → Vector (абсолютная позиция)
        std::uintptr_t m_pCameraServices = 0x1150; // C_BasePlayerPawn → CPlayer_CameraServices*
        std::uintptr_t m_vecViewOffset   = 0x10D8; // CPlayer_CameraServices → Vector (высота глаз)

        // --- устройство энтити-листа Source 2 (меняется крайне редко) ---
        // listEntry = entityList + 0x10 + 8 * (index >> 9)
        // element   = listEntry  + 0x78 * (index & 0x1FF)
        std::uintptr_t listEntryOffset = 0x10;
        std::uintptr_t entryStride     = 0x78;
    };

    // Живые значения: встроенные по умолчанию, либо из neverwin.ini.
    extern Offsets g;

    // Читает секцию [offsets] из ini. Возвращает true, если файл прочитан
    // и главные модульные оффсеты прошли базовую валидацию.
    bool LoadFromIni(const wchar_t* iniPath);
}
