#include "pch.h"
#include "offsets.hpp"

namespace offsets {

    Offsets g;

    namespace {
        const wchar_t* s_iniPath = nullptr;

        // Читает ключ из секции [offsets] как hex. Если ключа нет, значение
        // пустое или не hex — возвращает fallback (встроенное значение).
        std::uintptr_t ReadHex(const wchar_t* key, std::uintptr_t fallback) {
            if (!s_iniPath)
                return fallback;

            wchar_t buf[64]{};
            const DWORD len = GetPrivateProfileStringW(L"offsets", key, L"", buf, 63, s_iniPath);
            if (len == 0 || len >= 63)
                return fallback;

            wchar_t* end = nullptr;
            const unsigned long long v = wcstoull(buf, &end, 16);
            if (end == buf || *end != L'\0')
                return fallback;

            return static_cast<std::uintptr_t>(v);
        }

        // Модульные оффсеты указывают внутрь образов модулей, т.е. это
        // миллионы байт. Всё, что меньше 0x400, отсекаем как мусор.
        bool Plausible(std::uintptr_t v) { return v >= 0x400; }
    }

    bool LoadFromIni(const wchar_t* iniPath) {
        s_iniPath = iniPath;
        if (!iniPath || GetFileAttributesW(iniPath) == INVALID_FILE_ATTRIBUTES)
            return false;

        Offsets o;
        o.dwEntityList      = ReadHex(L"dwEntityList",      o.dwEntityList);
        o.dwLocalPlayerPawn = ReadHex(L"dwLocalPlayerPawn", o.dwLocalPlayerPawn);
        o.dwViewAngles      = ReadHex(L"dwViewAngles",      o.dwViewAngles);
        o.dwLocalPlayerController = ReadHex(L"dwLocalPlayerController", o.dwLocalPlayerController);
        o.highestEntityIndexOffset = ReadHex(L"highestEntityIndexOffset", o.highestEntityIndexOffset);
        o.m_iHealth         = ReadHex(L"m_iHealth",         o.m_iHealth);
        o.m_lifeState       = ReadHex(L"m_lifeState",       o.m_lifeState);
        o.m_iTeamNum        = ReadHex(L"m_iTeamNum",        o.m_iTeamNum);
        o.m_fFlags          = ReadHex(L"m_fFlags",          o.m_fFlags);
        o.m_pGameSceneNode  = ReadHex(L"m_pGameSceneNode",  o.m_pGameSceneNode);
        o.m_vecViewOffset   = ReadHex(L"m_vecViewOffset",   o.m_vecViewOffset);
        o.m_vecAbsOrigin    = ReadHex(L"m_vecAbsOrigin",    o.m_vecAbsOrigin);
        o.m_bDormant        = ReadHex(L"m_bDormant",        o.m_bDormant);
        o.m_pCameraServices = ReadHex(L"m_pCameraServices", o.m_pCameraServices);
        o.m_pWeaponServices = ReadHex(L"m_pWeaponServices", o.m_pWeaponServices);
        o.m_bWaitForNoAttack = ReadHex(L"m_bWaitForNoAttack", o.m_bWaitForNoAttack);
        o.m_bGunGameImmunity = ReadHex(L"m_bGunGameImmunity", o.m_bGunGameImmunity);
        o.m_iIDEntIndex     = ReadHex(L"m_iIDEntIndex",     o.m_iIDEntIndex);
        o.m_hPlayerPawn     = ReadHex(L"m_hPlayerPawn",     o.m_hPlayerPawn);
        o.m_bPawnIsAlive    = ReadHex(L"m_bPawnIsAlive",    o.m_bPawnIsAlive);
        o.m_hController     = ReadHex(L"m_hController",     o.m_hController);
        o.m_nTickBase       = ReadHex(L"m_nTickBase",       o.m_nTickBase);
        o.m_CommandContext  = ReadHex(L"m_CommandContext",  o.m_CommandContext);
        o.m_vecCsViewPunchAngle = ReadHex(L"m_vecCsViewPunchAngle", o.m_vecCsViewPunchAngle);
        o.m_hActiveWeapon   = ReadHex(L"m_hActiveWeapon",   o.m_hActiveWeapon);
        o.m_iClip1          = ReadHex(L"m_iClip1",          o.m_iClip1);
        o.m_nNextPrimaryAttackTick = ReadHex(L"m_nNextPrimaryAttackTick", o.m_nNextPrimaryAttackTick);
        o.m_bInReload       = ReadHex(L"m_bInReload",       o.m_bInReload);
        o.m_pWeaponVData    = ReadHex(L"m_pWeaponVData",    o.m_pWeaponVData);
        o.m_nDamage         = ReadHex(L"m_nDamage",         o.m_nDamage);
        o.m_flRange         = ReadHex(L"m_flRange",         o.m_flRange);
        o.m_flRangeModifier = ReadHex(L"m_flRangeModifier", o.m_flRangeModifier);
        o.m_flPenetration   = ReadHex(L"m_flPenetration",   o.m_flPenetration);
        o.m_flArmorRatio    = ReadHex(L"m_flArmorRatio",    o.m_flArmorRatio);
        o.m_flSpread        = ReadHex(L"m_flSpread",        o.m_flSpread);
        o.m_bIsScoped       = ReadHex(L"m_bIsScoped",       o.m_bIsScoped);
        o.m_ArmorValue      = ReadHex(L"m_ArmorValue",      o.m_ArmorValue);
        o.m_flSimulationTime = ReadHex(L"m_flSimulationTime", o.m_flSimulationTime);
        o.m_vecAbsVelocity  = ReadHex(L"m_vecAbsVelocity",  o.m_vecAbsVelocity);
        o.listEntryOffset   = ReadHex(L"listEntryOffset",   o.listEntryOffset);
        o.entryStride       = ReadHex(L"entryStride",       o.entryStride);

        // Главные модульные оффсеты обязаны быть правдоподобными, иначе
        // ini мусорный — остаёмся на встроенных значениях.
        if (!Plausible(o.dwEntityList) || !Plausible(o.dwLocalPlayerPawn) || !Plausible(o.dwViewAngles))
            return false;

        g = o;
        return true;
    }
}
