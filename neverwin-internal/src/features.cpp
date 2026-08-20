#include "pch.h"
#include "features.hpp"
#include "gui.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "offsets.hpp"

#include <random>

Features   g_features;
DebugState g_state;

namespace {

    struct Vector2 { float x = 0.0f; float y = 0.0f; };

    // --- Хоткеи. Маппинг соответствует оригинальному internal.txt. ---
    void HandleHotkeys() {
        if (GetAsyncKeyState(VK_F1) & 1) g_features.antiAimbot.store(!g_features.antiAimbot.load());
        if (GetAsyncKeyState(VK_F2) & 1) g_features.antiAimless.store(!g_features.antiAimless.load());
        if (GetAsyncKeyState(VK_F3) & 1) g_features.visualRecoil.store(!g_features.visualRecoil.load());
        if (GetAsyncKeyState(VK_F4) & 1) g_features.antiBhop.store(!g_features.antiBhop.load());
        if (GetAsyncKeyState(VK_F5) & 1) g_features.gamesense.store(!g_features.gamesense.load());
        if (GetAsyncKeyState(VK_INSERT) & 1) gui::g_menuOpen.store(!gui::g_menuOpen.load());
    }

    // --- Энтити-лист Source 2: список списков. ---
    // listEntry = entityList + 0x10 + 8 * (index >> 9)
    // element   = listEntry  + 0x78 * (index & 0x1FF)
    //
    // В оригинале формула для цикла врагов была перепутана:
    // ((8 * (i & 0x7FFF)) >> 9) + 16 == 16 + (i >> 6), тогда как правильно
    // 0x10 + 8 * (i >> 9). При (i & 0x1FF) >= 64 читался чужой listEntry,
    // и любой мусорный pawn дальше ронял процесс.
    uintptr_t GetEntityByHandle(uintptr_t entityList, uint32_t handle) {
        const uint32_t index = handle & 0x7FFF;
        const uintptr_t listEntry =
            mem::Read<uintptr_t>(entityList + offsets::listEntryOffset + 8ull * (index >> 9));
        if (!listEntry)
            return 0;
        return mem::Read<uintptr_t>(listEntry + offsets::entryStride * (index & 0x1FF));
    }

    void NormalizeAngles(float& pitch, float& yaw) {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
    }

    // Нажатие 'G' (дроп оружия). keybd_event из оригинала заменён на SendInput.
    void PressDropKey() {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0x47; // 'G'
        SendInput(1, &input, sizeof(INPUT));
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void RunFeatureLoop() {
    // 1. Ждём client.dll. Не вечно: если DLL инжектнули не в CS2,
    //    в оригинале она молча висела — теперь честно логируем и выходим.
    uintptr_t clientBase = 0;
    for (int attempts = 0; attempts < 1200; ++attempts) {
        clientBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"client.dll"));
        if (clientBase)
            break;
        Sleep(100);
    }
    if (!clientBase) {
        NW_LOG(L"ERROR: client.dll не найден за 120 сек. DLL не в CS2? Выгружаюсь.");
        return;
    }

    g_state.clientBase.store(clientBase);
    NW_LOG(L"client.dll @ 0x%llX", static_cast<unsigned long long>(clientBase));
    NW_LOG(L"оффсеты: EntityList=0x%llX LocalPlayer=0x%llX ViewAngles=0x%llX",
           static_cast<unsigned long long>(offsets::dwEntityList),
           static_cast<unsigned long long>(offsets::dwLocalPlayerPawn),
           static_cast<unsigned long long>(offsets::dwViewAngles));

    const uintptr_t entityListPtr  = clientBase + offsets::dwEntityList;
    const uintptr_t localPlayerPtr = clientBase + offsets::dwLocalPlayerPawn;
    const uintptr_t viewAnglesPtr  = clientBase + offsets::dwViewAngles;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dropChance(1, 100);
    std::uniform_real_distribution<float> randAim(-15.0f, 15.0f);

    int previousAmmo = -1;
    Vector2 oldPunch{};

    for (;;) {
        Sleep(1);
        HandleHotkeys();
        if (gui::g_unloadRequested.load())
            return;

        const uintptr_t localPlayer = mem::Read<uintptr_t>(localPlayerPtr);
        g_state.localPlayer.store(localPlayer);
        if (!localPlayer)
            continue;

        const int health = mem::Read<int>(localPlayer + offsets::m_iHealth);
        g_state.localHealth.store(health);
        if (health <= 0)
            continue;

        const uintptr_t entityList = mem::Read<uintptr_t>(entityListPtr);
        g_state.entityList.store(entityList);
        if (!entityList)
            continue;

        const int localTeam = mem::Read<int>(localPlayer + offsets::m_iTeamNum);
        g_state.localTeam.store(localTeam);

        // --- 1. Антибхоп: пока нажат пробел — снимаем FL_ONGROUND (бит 0). ---
        if (g_features.antiBhop.load() && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            const uint32_t flags = mem::Read<uint32_t>(localPlayer + offsets::m_fFlags);
            if ((flags & 1u) != 0u) {
                mem::Write<uint32_t>(localPlayer + offsets::m_fFlags, flags & ~1u);
            }
        }

        // --- 2. Gamesense: 20% шанс дропа оружия при выстреле/перезарядке. ---
        if (g_features.gamesense.load()) {
            const uint32_t weaponHandle = mem::Read<uint32_t>(localPlayer + offsets::m_pClippingWeapon);
            if (weaponHandle) {
                const uintptr_t weapon = GetEntityByHandle(entityList, weaponHandle);
                if (weapon) {
                    const int  currentAmmo = mem::Read<int>(weapon + offsets::m_iClip1);
                    const bool isReloading = mem::Read<uint8_t>(weapon + offsets::m_bInReload) != 0;

                    if ((previousAmmo != -1 && currentAmmo < previousAmmo) || isReloading) {
                        if (dropChance(gen) <= 20) {
                            PressDropKey();
                            Sleep(300);
                        }
                    }
                    previousAmmo = currentAmmo;
                }
            }
        }

        // --- 3. Visual recoil x4. ---
        if (g_features.visualRecoil.load()) {
            const Vector2 punch = mem::Read<Vector2>(localPlayer + offsets::m_aimPunchAngle);
            Vector2 newPunch{ punch.x * 4.0f, punch.y * 4.0f };

            Vector2 view = mem::Read<Vector2>(viewAnglesPtr);
            view.x -= (newPunch.x - oldPunch.x);
            view.y -= (newPunch.y - oldPunch.y);
            NormalizeAngles(view.x, view.y);

            if (mem::Write<Vector2>(viewAnglesPtr, view)) {
                oldPunch = newPunch;
                g_state.viewAnglesWritable.store(true);
            } else {
                // Регион не открылся на запись (например, оффсет стух) —
                // не долбим его каждый тик, просто логируем один раз.
                static bool logged = false;
                if (!logged) {
                    NW_LOG(L"WARNING: viewAngles не пишутся (0x%llX) — проверь dwViewAngles.",
                           static_cast<unsigned long long>(viewAnglesPtr));
                    logged = true;
                }
                g_state.viewAnglesWritable.store(false);
            }
        } else {
            oldPunch = {};
        }

        // --- 4. Антиаимбот / антиаимлесс: если виден враг — портим свой прицел. ---
        if (g_features.antiAimbot.load() || g_features.antiAimless.load()) {
            bool enemySpotted = false;
            for (uint32_t i = 1; i < 64; ++i) {
                const uintptr_t pawn = GetEntityByHandle(entityList, i);
                if (!pawn || pawn == localPlayer)
                    continue;

                const int enemyHealth = mem::Read<int>(pawn + offsets::m_iHealth);
                const int enemyTeam   = mem::Read<int>(pawn + offsets::m_iTeamNum);
                if (enemyHealth > 0 && enemyTeam != localTeam) {
                    enemySpotted = true;
                    break;
                }
            }

            if (enemySpotted) {
                Vector2 view = mem::Read<Vector2>(viewAnglesPtr);
                if (g_features.antiAimless.load()) {
                    view.x = 89.0f;
                    view.y += 15.0f;
                } else {
                    view.x += randAim(gen);
                    view.y += randAim(gen);
                }
                NormalizeAngles(view.x, view.y);
                mem::Write<Vector2>(viewAnglesPtr, view);
            }
        }
    }
}
