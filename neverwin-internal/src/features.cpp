#include "pch.h"
#include "features.hpp"
#include "entities.hpp"
#include "gui.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "offsets.hpp"
#include "shared_state.hpp"

#include <cmath>
#include <cfloat>
#include <random>

Features   g_features;
DebugState g_state;

namespace {

    // Алиас на живые оффсеты: встроенные дефолты либо значения из neverwin.ini.
    const auto& off = offsets::g;

    struct Vector2 { float x = 0.0f; float y = 0.0f; };

    // --- Хоткеи. Маппинг соответствует оригинальному internal.txt. ---
    void HandleHotkeys() {
        if (GetAsyncKeyState(VK_F1) & 1) g_features.antiAimbot.store(!g_features.antiAimbot.load());
        if (GetAsyncKeyState(VK_F2) & 1) g_features.antiAimless.store(!g_features.antiAimless.load());
        if (GetAsyncKeyState(VK_F3) & 1) g_features.visualRecoil.store(!g_features.visualRecoil.load());
        if (GetAsyncKeyState(VK_F4) & 1) g_features.antiBhop.store(!g_features.antiBhop.load());
        if (GetAsyncKeyState(VK_F5) & 1) g_features.gamesense.store(!g_features.gamesense.load());
        if (GetAsyncKeyState(VK_F6) & 1) gui::g_hudVisible.store(!gui::g_hudVisible.load());
        // Тоггл меню клавишами — только когда in-game рендер НЕ встал (Vulkan).
        // Иначе тогглит WndProc-хук: двойной тоггл даст меню, которое само
        // закрывается мгновенно.
        if (!gui::g_inGameMenuReady.load()) {
            if (GetAsyncKeyState('P') & 1)
                gui::g_menuOpen.store(!gui::g_menuOpen.load());
            if (GetAsyncKeyState(VK_INSERT) & 1)
                gui::g_menuOpen.store(!gui::g_menuOpen.load());
        }
        // END — выгрузка: выставляет флаг, цикл фич завершается,
        // ShutdownAndExit освобождает DLL.
        if (GetAsyncKeyState(VK_END) & 1) gui::g_unloadRequested.store(true);
    }

    // --- Энтити-лист и углы — в entities.hpp (общие с хуком CreateMove). ---

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
           static_cast<unsigned long long>(off.dwEntityList),
           static_cast<unsigned long long>(off.dwLocalPlayerPawn),
           static_cast<unsigned long long>(off.dwViewAngles));

    const uintptr_t entityListPtr  = clientBase + off.dwEntityList;
    const uintptr_t localPlayerPtr = clientBase + off.dwLocalPlayerPawn;
    const uintptr_t viewAnglesPtr  = clientBase + off.dwViewAngles;

    // Внешний HUD-оверлей читает состояние отсюда.
    nwshared::Publisher shm;
    if (shm) {
        shm->dllVersion = NW_VERSION;
    } else {
        NW_LOG(L"WARNING: shared memory не создалась — внешний оверлей не увидит состояние.");
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dropChance(1, 100);

    int previousAmmo = -1;
    Vector2 oldPunch{};
    uint32_t lastAppliedCmd = 0; // последняя применённая команда оверлея

    for (;;) {
        Sleep(1);
        HandleHotkeys();
        if (gui::g_unloadRequested.load()) {
            if (shm) {
                shm->unloadRequested = 1;
                shm.Commit();
            }
            return;
        }

        const uintptr_t localPlayer = mem::Read<uintptr_t>(localPlayerPtr);
        g_state.localPlayer.store(localPlayer);
        if (!localPlayer)
            continue;

        const int health = mem::Read<int>(localPlayer + off.m_iHealth);
        g_state.localHealth.store(health);
        if (health <= 0)
            continue;

        const uintptr_t entityList = mem::Read<uintptr_t>(entityListPtr);
        g_state.entityList.store(entityList);
        if (!entityList)
            continue;

        const int localTeam = mem::Read<int>(localPlayer + off.m_iTeamNum);
        g_state.localTeam.store(localTeam);

        // Снапшот состояния для внешнего оверлея + приём его команд.
        if (shm) {
            shm->antiAimbot      = g_features.antiAimbot.load() ? 1u : 0u;
            shm->antiAimless     = g_features.antiAimless.load() ? 1u : 0u;
            shm->visualRecoil    = g_features.visualRecoil.load() ? 1u : 0u;
            shm->antiBhop        = g_features.antiBhop.load() ? 1u : 0u;
            shm->gamesense       = g_features.gamesense.load() ? 1u : 0u;
            shm->hudVisible      = gui::g_hudVisible.load() ? 1u : 0u;
            shm->menuOpen        = gui::g_menuOpen.load() ? 1u : 0u;
            shm->inGameMenu      = gui::g_inGameMenuReady.load() ? 1u : 0u;
            shm->unloadRequested = 0u;
            shm->clientBase      = clientBase;
            shm->entityList      = entityList;
            shm->localPlayer     = localPlayer;
            shm->localHealth     = health;
            shm->localTeam       = localTeam;
            shm->viewAnglesWritable = g_state.viewAnglesWritable.load() ? 1u : 0u;
            shm->offsetsFromIni     = g_state.offsetsFromIni.load() ? 1u : 0u;

            // Команды из меню оверлея (чекбоксы / кнопка выгрузки).
            const uint32_t cmd = shm->cmdSeq;
            if (cmd != lastAppliedCmd && shm->setMask != 0u) {
                const uint32_t mask = shm->setMask;
                const uint32_t val  = shm->setValues;
                if (mask & nwshared::kFbAntiAimbot)
                    g_features.antiAimbot.store((val & nwshared::kFbAntiAimbot) != 0);
                if (mask & nwshared::kFbAntiAimless)
                    g_features.antiAimless.store((val & nwshared::kFbAntiAimless) != 0);
                if (mask & nwshared::kFbVisualRecoil)
                    g_features.visualRecoil.store((val & nwshared::kFbVisualRecoil) != 0);
                if (mask & nwshared::kFbAntiBhop)
                    g_features.antiBhop.store((val & nwshared::kFbAntiBhop) != 0);
                if (mask & nwshared::kFbGamesense)
                    g_features.gamesense.store((val & nwshared::kFbGamesense) != 0);
                if ((mask & nwshared::kFbUnload) && (val & nwshared::kFbUnload))
                    gui::g_unloadRequested.store(true);

                lastAppliedCmd = cmd;
                shm->appliedSeq = cmd;
            }

            shm.Commit();
        }

        // --- 1. Антибхоп: пока нажат пробел — снимаем FL_ONGROUND (бит 0). ---
        if (g_features.antiBhop.load() && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            const uint32_t flags = mem::Read<uint32_t>(localPlayer + off.m_fFlags);
            if ((flags & 1u) != 0u) {
                mem::Write<uint32_t>(localPlayer + off.m_fFlags, flags & ~1u);
            }
        }

        // --- 2. Gamesense: 20% шанс дропа оружия при выстреле/перезарядке. ---
        // Оружие теперь через сервисы: pawn -> m_pWeaponServices -> m_hActiveWeapon.
        if (g_features.gamesense.load()) {
            const uintptr_t weaponServices = mem::Read<uintptr_t>(localPlayer + off.m_pWeaponServices);
            if (weaponServices) {
                const uint32_t weaponHandle = mem::Read<uint32_t>(weaponServices + off.m_hActiveWeapon);
                if (weaponHandle) {
                    const uintptr_t weapon = ent::GetEntityByHandle(entityList, weaponHandle);
                    if (weapon) {
                        const int  currentAmmo = mem::Read<int>(weapon + off.m_iClip1);
                        const bool isReloading = mem::Read<uint8_t>(weapon + off.m_bInReload) != 0;

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
        }

        // --- 3. Visual recoil x4. ---
        // Панч отдачи больше не лежит в павне: он в camera services
        // (m_vecCsViewPunchAngle), читаем через m_pCameraServices.
        if (g_features.visualRecoil.load()) {
            Vector2 punch{};
            const uintptr_t cameraServices = mem::Read<uintptr_t>(localPlayer + off.m_pCameraServices);
            if (cameraServices)
                punch = mem::Read<Vector2>(cameraServices + off.m_vecCsViewPunchAngle);
            Vector2 newPunch{ punch.x * 4.0f, punch.y * 4.0f };

            Vector2 view = mem::Read<Vector2>(viewAnglesPtr);
            view.x -= (newPunch.x - oldPunch.x);
            view.y -= (newPunch.y - oldPunch.y);
            ent::NormalizeAngles(view.x, view.y);

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

        // --- 4a. Реверс аимбот (F1): наводка на ближайшего живого тиммейта. ---
        // Прямая запись viewAngles из цикла фич — тот же метод, что был до
        // переноса на CreateMove. F2 так работал (камера в пол + кручение),
        // значит прямые записи до игры доходят. Канал через user cmd убран:
        // на твоём клиенте хук CreateMove не встал (см. лог), он был мёртвым.
        if (g_features.antiAimbot.load()) {
            const ent::Vector3 eye = ent::GetEyePosition(localPlayer);
            if (eye.x != 0.0f || eye.y != 0.0f || eye.z != 0.0f) {
                uintptr_t bestPawn = 0;
                ent::Vector3 bestOrigin{};
                float bestDist2 = FLT_MAX;
                int teammates = 0;

                for (uint32_t i = 1; i < 64; ++i) {
                    const uintptr_t pawn = ent::GetEntityByHandle(entityList, i);
                    if (!pawn || pawn == localPlayer)
                        continue;
                    if (mem::Read<int>(pawn + off.m_iHealth) <= 0)
                        continue;
                    if (mem::Read<int>(pawn + off.m_iTeamNum) != localTeam)
                        continue;

                    ++teammates;
                    const uintptr_t sceneNode = mem::Read<uintptr_t>(pawn + off.m_pGameSceneNode);
                    if (!sceneNode)
                        continue;
                    const ent::Vector3 origin = mem::Read<ent::Vector3>(sceneNode + off.m_vecAbsOrigin);
                    if (origin.x == 0.0f && origin.y == 0.0f && origin.z == 0.0f)
                        continue;

                    const float dx = origin.x - eye.x;
                    const float dy = origin.y - eye.y;
                    const float dz = origin.z - eye.z;
                    const float dist2 = dx * dx + dy * dy + dz * dz;
                    if (dist2 < bestDist2) {
                        bestDist2 = dist2;
                        bestPawn = pawn;
                        bestOrigin = origin;
                    }
                }

                if (bestPawn) {
                    // +64 юнита вверх от origin — корпус/голова.
                    const ent::Vector3 target{ bestOrigin.x, bestOrigin.y, bestOrigin.z + 64.0f };
                    ent::Vector2 angles = ent::CalcAngles(eye, target);
                    ent::NormalizeAngles(angles.x, angles.y);
                    mem::Write<float>(viewAnglesPtr, angles.x);
                    mem::Write<float>(viewAnglesPtr + 4, angles.y);

                    static uint32_t lastLog = 0;
                    const uint32_t now = GetTickCount();
                    if (now - lastLog > 5000) {
                        lastLog = now;
                        NW_LOG(L"F1: тиммейтов %d, цель (%.0f %.0f %.0f), углы (%.1f, %.1f)",
                               teammates, bestOrigin.x, bestOrigin.y, bestOrigin.z,
                               angles.x, angles.y);
                    }
                } else {
                    static uint32_t lastNone = 0;
                    const uint32_t now = GetTickCount();
                    if (now - lastNone > 5000) {
                        lastNone = now;
                        NW_LOG(L"F1: живых тиммейтов не найдено (%d в списке) — наводиться не на кого.",
                               teammates);
                    }
                }
            } else {
                static uint32_t lastEye = 0;
                const uint32_t now = GetTickCount();
                if (now - lastEye > 5000) {
                    lastEye = now;
                    NW_LOG(L"F1: глаза не прочитались (scene node / origin / viewOffset) — позиционные оффсеты стухли?");
                }
            }
        }

        // --- 4b. Антиаимлесс (F2): виден враг — взгляд в пол. ---
        // Тот же метод, что работал до переноса: прямая запись viewAngles.
        if (g_features.antiAimless.load()) {
            bool enemySpotted = false;
            for (uint32_t i = 1; i < 64; ++i) {
                const uintptr_t pawn = ent::GetEntityByHandle(entityList, i);
                if (!pawn || pawn == localPlayer)
                    continue;

                const int enemyHealth = mem::Read<int>(pawn + off.m_iHealth);
                const int enemyTeam   = mem::Read<int>(pawn + off.m_iTeamNum);
                if (enemyHealth > 0 && enemyTeam != localTeam) {
                    enemySpotted = true;
                    break;
                }
            }

            if (enemySpotted) {
                const float curYaw = mem::Read<float>(viewAnglesPtr + 4);
                float newYaw = curYaw + 15.0f;
                if (newYaw > 180.0f) newYaw -= 360.0f;
                if (newYaw < -180.0f) newYaw += 360.0f;
                mem::Write<float>(viewAnglesPtr, 89.0f);
                mem::Write<float>(viewAnglesPtr + 4, newYaw);

                static bool logged = false;
                if (!logged) {
                    NW_LOG(L"F2: враг виден — камера в пол + кручение.");
                    logged = true;
                }
            }
        }
    }
}
