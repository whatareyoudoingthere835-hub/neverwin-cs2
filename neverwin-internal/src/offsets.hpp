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
        std::uintptr_t dwEntityList      = 0x2555050; // CGameEntitySystem::m_list
        std::uintptr_t dwLocalPlayerPawn = 0x23AA118;
        std::uintptr_t dwViewAngles      = 0x23C01A8;
        std::uintptr_t dwLocalPlayerController = 0x2384DB0; // контроллер (цепочка user cmd, raimv2)

        // --- C_BaseEntity (и все наследники) ---
        std::uintptr_t m_iHealth         = 0x34C;
        std::uintptr_t m_lifeState       = 0x354;  // 0=жив, 1=умирает, 2=мёртв
        std::uintptr_t m_iTeamNum        = 0x3E7;
        std::uintptr_t m_fFlags          = 0x3F4;  // бит 0 = FL_ONGROUND
        std::uintptr_t m_pGameSceneNode  = 0x330;  // C_BaseEntity → CGameSceneNode*
        std::uintptr_t m_vecViewOffset   = 0x0E78; // C_BaseModelEntity → Vector (высота глаз)

        // --- CGameSceneNode ---
        std::uintptr_t m_vecAbsOrigin    = 0xC8;   // Vector (абсолютная позиция)

        // --- C_BasePlayerPawn ---
        std::uintptr_t m_pCameraServices = 0x1240; // CPlayer_CameraServices*
        std::uintptr_t m_pWeaponServices = 0x1208; // CPlayer_WeaponServices*

        // --- CBasePlayerController: контекст команд (канал raimv2) ---
        std::uintptr_t m_CommandContext = 0x608;  // CCommandContext* (командное кольцо)

        // --- CPlayer_CameraServices: визуальный панч отдачи ---
        // (замена m_aimPunchAngle — поле переехало из павна в камеру)
        std::uintptr_t m_vecCsViewPunchAngle = 0x48; // QAngle (pitch, yaw)

        // --- CPlayer_WeaponServices: активное оружие ---
        // (замена m_pClippingWeapon — теперь оружие через сервис)
        std::uintptr_t m_hActiveWeapon   = 0x60;   // CHandle<C_BasePlayerWeapon> (u32)

        // --- оружие ---
        std::uintptr_t m_iClip1          = 0x1700; // C_BasePlayerWeapon
        std::uintptr_t m_bInReload       = 0x1814; // C_CSWeaponBase

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
