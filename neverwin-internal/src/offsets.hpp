#pragma once
#include <cstdint>

// ============================================================================
// ОФФСЕТЫ — ЕДИНСТВЕННОЕ МЕСТО, КОТОРОЕ НУЖНО ПРАВИТЬ ПОСЛЕ АПДЕЙТА CS2.
//
// Значения взяты из дампа схем в internal.txt. CS2 обновляет бинарь почти
// каждую неделю, и оффсеты устаревают. Если после обновления игры DLL
// "ничего не делает" или падает — первым делом сверяй их со свежим дампом.
// ============================================================================
namespace offsets {

    // --- client.dll ---
    constexpr std::uintptr_t dwEntityList      = 0x2554050; // CGameEntitySystem::m_list
    constexpr std::uintptr_t dwLocalPlayerPawn = 0x23A9118;
    constexpr std::uintptr_t dwViewAngles      = 0x23BF1A8;

    // --- схема C_CSPlayerPawnBase ---
    constexpr std::uintptr_t m_iHealth         = 0x34C;
    constexpr std::uintptr_t m_iTeamNum        = 0x3E7;
    constexpr std::uintptr_t m_fFlags          = 0x3F4;  // бит 0 = FL_ONGROUND
    constexpr std::uintptr_t m_aimPunchAngle   = 0x14CC; // QAngle (pitch, yaw)
    constexpr std::uintptr_t m_pClippingWeapon = 0x1308; // CEntityHandle (u32)
    constexpr std::uintptr_t m_iClip1          = 0x15A4;
    constexpr std::uintptr_t m_bInReload       = 0x1704;

    // --- устройство энтити-листа Source 2 ---
    // listEntry = entityList + 0x10 + 8 * (index >> 9)
    // element   = listEntry  + 0x78 * (index & 0x1FF)
    constexpr std::uintptr_t listEntryOffset = 0x10;
    constexpr std::uintptr_t entryStride     = 0x78; // 120 байт
}
