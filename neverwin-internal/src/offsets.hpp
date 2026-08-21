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
        // CGameEntitySystem::m_hHighestEntityIndex. Нужен только запасному
        // scanner-у F1, когда controller->m_hPlayerPawn временно не резолвится.
        std::uintptr_t highestEntityIndexOffset = 0x2090;

        // --- C_BaseEntity (и все наследники) ---
        std::uintptr_t m_iHealth         = 0x34C;
        std::uintptr_t m_lifeState       = 0x354;  // 0=жив, 1=умирает, 2=мёртв
        std::uintptr_t m_iTeamNum        = 0x3E7;
        std::uintptr_t m_fFlags          = 0x3F4;  // бит 0 = FL_ONGROUND
        std::uintptr_t m_pGameSceneNode  = 0x330;  // C_BaseEntity → CGameSceneNode*
        std::uintptr_t m_vecViewOffset   = 0x0E78; // C_BaseModelEntity → Vector (высота глаз)

        // --- CGameSceneNode ---
        std::uintptr_t m_vecAbsOrigin    = 0xC8;   // Vector (абсолютная позиция)
        std::uintptr_t m_bDormant        = 0x103;  // bool (дормант — игрок вне PVS, хп может быть 0)

        // --- C_BasePlayerPawn / C_CSPlayerPawnBase / C_CSPlayerPawn ---
        std::uintptr_t m_pCameraServices = 0x1240; // CPlayer_CameraServices*
        std::uintptr_t m_pWeaponServices = 0x1208; // CPlayer_WeaponServices*
        std::uintptr_t m_bWaitForNoAttack = 0x1C98; // запрет выстрела после смены/респавна
        std::uintptr_t m_bGunGameImmunity = 0x3268; // spawn protection
        std::uintptr_t m_iIDEntIndex       = 0x342C; // сущность под прицелом

        // --- CCSPlayerController / CBasePlayerController ---
        // Единственный надёжный путь перечисления игроков:
        // controller -> m_hPlayerPawn -> pawn + согласованный alive-флаг.
        std::uintptr_t m_hPlayerPawn      = 0x914;
        std::uintptr_t m_bPawnIsAlive     = 0x91C;
        // Обратная связь pawn -> controller; используется для верифицированного
        // fallback-скана reverse aim.
        std::uintptr_t m_hController      = 0x13D0;
        std::uintptr_t m_nTickBase        = 0x6B8;
        std::uintptr_t m_CommandContext   = 0x608;  // CCommandContext* (канал raimv2)

        // --- CPlayer_CameraServices: визуальный панч отдачи ---
        // (замена m_aimPunchAngle — поле переехало из павна в камеру)
        std::uintptr_t m_vecCsViewPunchAngle = 0x48; // QAngle (pitch, yaw)

        // --- CPlayer_WeaponServices: активное оружие ---
        // (замена m_pClippingWeapon — теперь оружие через сервис)
        std::uintptr_t m_hActiveWeapon   = 0x60;   // CHandle<C_BasePlayerWeapon> (u32)

        // --- оружие ---
        std::uintptr_t m_iClip1          = 0x1700; // C_BasePlayerWeapon
        std::uintptr_t m_nNextPrimaryAttackTick = 0x16F0; // следующий разрешённый тик выстрела
        std::uintptr_t m_bInReload       = 0x1814; // C_CSWeaponBase
        std::uintptr_t m_pWeaponVData    = 0x388;  // C_BasePlayerWeapon -> CCSWeaponBaseVData* (weapon_data)

        // --- CCSWeaponBaseVData ---
        std::uintptr_t m_nDamage         = 0x828;
        std::uintptr_t m_flRange         = 0x838;
        std::uintptr_t m_flRangeModifier = 0x83C;
        std::uintptr_t m_flPenetration   = 0x834;
        std::uintptr_t m_flArmorRatio    = 0x830;
        std::uintptr_t m_flSpread        = 0x758;

        // --- C_CSPlayerPawn ---
        std::uintptr_t m_bIsScoped       = 0x1C78;
        std::uintptr_t m_ArmorValue      = 0x1CA4;
        std::uintptr_t m_flSimulationTime = 0x3B8;
        std::uintptr_t m_vecAbsVelocity  = 0x3F8;

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
