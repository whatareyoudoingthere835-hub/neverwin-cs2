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
        // F1 циклит: выкл -> raimv1 -> raimv2 -> выкл.
        if (GetAsyncKeyState(VK_F1) & 1) {
            const int m = g_features.reverseAim.load();
            g_features.reverseAim.store((m + 1) % 3);
        }
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

    // ========================================================================
    // Raimv2: запись углов в юзеркоманду (цепочка velocity/quint, без вызовов).
    //
    // Раскладка протобуфа гуляет между источниками, поэтому НЕ верим никому:
    // перебираем кандидатов (velocity: pb@0x20, base@0x10, msg@0x20,
    // углы@msg+0x08, биты u32@msg+0; quint: pb@0x18, base@0x28, msg@0x30,
    // углы@msg+0x18, биты u64@msg+0x10) и выбираем раскладку, чьи углы
    // совпадают с живыми dwViewAngles. Победитель логируется.
    // ========================================================================
    struct CmdLayout {
        uint32_t ringBase = 0;  // смещение кольца команд в контексте
        uint32_t seqOff   = 0;  // номер/последовательность текущей команды
        uint32_t pbOff    = 0;  // cmd -> csgo_usercmd_pb
        uint32_t baseOff  = 0;  // pb -> base_usercmd_pb
        uint32_t msgOff   = 0;  // base -> msg_qangle* (m_viewangles)
        uint32_t angOff   = 0;  // msg -> QAngle (x/y)
        bool     velocityStyle = false; // true: биты u32@msg+0; false: u64@msg+0x10
        bool     resolved = false;
    } g_cmdLayout;

    uint32_t g_cmdFails = 0;

    bool Raimv2ResolveLayout(uintptr_t ctx, const float livePitch, const float liveYaw) {
        if (!ent::IsSaneAngles(livePitch, liveYaw))
            return false;

        struct Cand {
            CmdLayout l;
            float score = FLT_MAX;
        } best;

        const uint32_t seqOffs[]  = { 0x5910, 0x59A8 }; // velocity / quint
        const uint32_t ringBases[] = {
            0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0,
            0x100, 0x140, 0x180, 0x1C0, 0x200, 0x280, 0x300, 0x400,
            0x500, 0x800, 0x1000,
        };
        const uint32_t pbOffs[]   = { 0x18, 0x20 };       // quint / velocity
        const uint32_t baseOffs[] = { 0x28, 0x10 };       // quint / velocity
        const uint32_t msgOffs[]  = { 0x30, 0x20 };       // quint / velocity
        const uint32_t angOffs[]  = { 0x18, 0x08 };       // quint / velocity

        for (uint32_t seqOff : seqOffs) {
            const int seq = mem::Read<int>(ctx + seqOff);
            if (seq <= 0 || seq > 1000000)
                continue;
            for (uint32_t ringBase : ringBases) {
                const uintptr_t cmd = ctx + ringBase + static_cast<uintptr_t>(seq % 150) * 0x98;
                for (uint32_t pbOff : pbOffs) {
                    const uintptr_t pb = mem::Read<uintptr_t>(cmd + pbOff);
                    if (!pb)
                        continue;
                    for (uint32_t baseOff : baseOffs) {
                        const uintptr_t base = mem::Read<uintptr_t>(pb + baseOff);
                        if (!base)
                            continue;
                        for (uint32_t msgOff : msgOffs) {
                            const uintptr_t msg = mem::Read<uintptr_t>(base + msgOff);
                            if (!msg)
                                continue;
                            for (uint32_t angOff : angOffs) {
                                const ent::Vector2 ang = mem::Read<ent::Vector2>(msg + angOff);
                                if (!ent::IsSaneAngles(ang.x, ang.y))
                                    continue;
                                float dy = std::fabsf(ang.y - liveYaw);
                                if (dy > 180.0f)
                                    dy = 360.0f - dy;
                                const float score = std::fabsf(ang.x - livePitch) + dy;
                                if (score < best.score) {
                                    CmdLayout l{};
                                    l.ringBase = ringBase;
                                    l.seqOff   = seqOff;
                                    l.pbOff    = pbOff;
                                    l.baseOff  = baseOff;
                                    l.msgOff   = msgOff;
                                    l.angOff   = angOff;
                                    l.velocityStyle = (angOff == 0x08);
                                    l.resolved = true;
                                    best.l = l;
                                    best.score = score;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (best.score <= 25.0f) {
            g_cmdLayout = best.l;
            NW_LOG(L"raimv2: раскладка user cmd ring=0x%X seq=0x%X pb=0x%X base=0x%X msg=0x%X ang=0x%X (%s стиль, отклонение %.1f°)",
                   best.l.ringBase, best.l.seqOff, best.l.pbOff, best.l.baseOff,
                   best.l.msgOff, best.l.angOff,
                   best.l.velocityStyle ? L"velocity" : L"quint", best.score);
            return true;
        }
        return false;
    }

    // Запись углов в текущую юзеркоманду. Только чтения по оффсетам из дампа,
    // вызовов клиента нет. Возвращает false при любом сбое.
    bool Raimv2WriteToCmd(uintptr_t clientBase, float pitch, float yaw) {
        const uintptr_t controller =
            mem::Read<uintptr_t>(clientBase + off.dwLocalPlayerController);
        if (!controller)
            return false;
        const uintptr_t ctx = mem::Read<uintptr_t>(controller + off.m_CommandContext);
        if (!ctx)
            return false;

        const float livePitch = mem::Read<float>(clientBase + off.dwViewAngles);
        const float liveYaw   = mem::Read<float>(clientBase + off.dwViewAngles + 4);

        if (!g_cmdLayout.resolved && !Raimv2ResolveLayout(ctx, livePitch, liveYaw))
            return false;

        const int seq = mem::Read<int>(ctx + g_cmdLayout.seqOff);
        if (seq <= 0 || seq > 1000000)
            return false;
        const uintptr_t cmd = ctx + g_cmdLayout.ringBase + static_cast<uintptr_t>(seq % 150) * 0x98;
        const uintptr_t pb = mem::Read<uintptr_t>(cmd + g_cmdLayout.pbOff);
        if (!pb)
            return false;
        const uintptr_t base = mem::Read<uintptr_t>(pb + g_cmdLayout.baseOff);
        if (!base)
            return false;
        const uintptr_t msg = mem::Read<uintptr_t>(base + g_cmdLayout.msgOff);
        if (!msg)
            return false;

        const ent::Vector2 ang{ pitch, yaw };
        if (!mem::Write<ent::Vector2>(msg + g_cmdLayout.angOff, ang))
            return false;

        // Биты «поля заданы»: у velocity — u32 has_bits в начале msg,
        // у quint — u64 cached_bits на +0x10.
        if (g_cmdLayout.velocityStyle) {
            const uint32_t bits = mem::Read<uint32_t>(msg);
            mem::Write<uint32_t>(msg, bits | 0x3u);
        } else {
            const uint64_t bits = mem::Read<uint64_t>(msg + 0x10);
            mem::Write<uint64_t>(msg + 0x10, bits | 7u);
        }
        return true;
    }

    // Цель реверс аима: ближайший ЖИВОЙ тиммейт. Трупы не берутся вообще —
    // фолбэк «целиться в труп» убран: прицел вёл на мертвецов, когда живых
    // не было. Стены и дальность не проверяются.
    //
    // Скан по 512 хэндлам: игроки обычно в начале списка, но широкий скан
    // ничего не стоит. Команда читается через ent::GetTeam (uint8!) —
    // int-чтение ломало фильтр тиммейтов на части павнов.
    //
    // Диагностика: сколько павнов/врагов/тимейтов/нод/origin увидели —
    // по этим числам видно, на каком шаге цепочка рвётся, если рвётся.
    struct TeamScanStats {
        int pawns = 0, enemies = 0, teammates = 0, withNode = 0, withOrigin = 0;
    };

    bool FindTeammateTarget(uintptr_t localPlayer, uintptr_t entityList,
                            uint8_t localTeam, ent::Vector3& outOrigin,
                            int& outHealth,
                            int& aliveCount, int& totalCount,
                            TeamScanStats& stats) {
        uintptr_t bestPawn = 0;
        ent::Vector3 bestOrigin{};
        float bestDist2 = FLT_MAX;
        int bestHealth = 0;

        const ent::Vector3 eye = ent::GetEyePosition(localPlayer);

        for (uint32_t i = 1; i < 512; ++i) {
            const uintptr_t pawn = ent::GetEntityByHandle(entityList, i);
            if (!pawn || pawn == localPlayer)
                continue;

            ++stats.pawns;
            const uint8_t team = ent::GetTeam(pawn);
            if (team != localTeam) {
                if (team != 0 && mem::Read<int>(pawn + off.m_iHealth) > 0)
                    ++stats.enemies;
                continue;
            }

            ++stats.teammates;
            const uintptr_t node = mem::Read<uintptr_t>(pawn + off.m_pGameSceneNode);
            if (!node)
                continue;
            ++stats.withNode;

            const ent::Vector3 origin = mem::Read<ent::Vector3>(node + off.m_vecAbsOrigin);
            if (origin.x == 0.0f && origin.y == 0.0f && origin.z == 0.0f)
                continue;
            ++stats.withOrigin;

            const float dx = origin.x - eye.x;
            const float dy = origin.y - eye.y;
            const float dz = origin.z - eye.z;
            const float d2 = dx * dx + dy * dy + dz * dz;

            // Ближе 64 юнитов (труп под камерой, тиммейт вплотную) — почти
            // вертикальные углы, бесполезно.
            if (d2 < 64.0f * 64.0f)
                continue;

            ++totalCount;
            // Живой: hp в разумных пределах. Трупы (hp=0) и мусор отсекаются.
            const int hp = mem::Read<int>(pawn + off.m_iHealth);
            if (hp <= 0 || hp > 1000)
                continue;

            ++aliveCount;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestPawn = pawn;
                bestOrigin = origin;
                bestHealth = hp;
            }
        }

        if (bestPawn) {
            outOrigin = bestOrigin;
            outHealth = bestHealth;
            return true;
        }
        return false;
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

        // Мёртвый/умирающий локальный: его труп остаётся в списке прямо под
        // камерой смерти — raim наводился на него и уводил прицел в зенит.
        // Пока локальный не жив (lifeState != 0) — фичи углов молчат.
        if (mem::Read<uint8_t>(localPlayer + off.m_lifeState) != 0)
            continue;

        const uintptr_t entityList = mem::Read<uintptr_t>(entityListPtr);
        g_state.entityList.store(entityList);
        if (!entityList)
            continue;

        const uint8_t localTeam = ent::GetTeam(localPlayer);
        g_state.localTeam.store(localTeam);

        // Снапшот состояния для внешнего оверлея + приём его команд.
        if (shm) {
            shm->reverseAim       = static_cast<uint8_t>(g_features.reverseAim.load());
            shm->antiAimless     = g_features.antiAimless.load() ? 1u : 0u;
            shm->spinSpeed       = static_cast<uint8_t>(g_features.spinSpeed.load() + 0.5f);
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

            // Команды из меню оверлея (комбо реверс аима / кнопка выгрузки).
            const uint32_t cmd = shm->cmdSeq;
            if (cmd != lastAppliedCmd && shm->setMask != 0u) {
                const uint32_t mask = shm->setMask;
                const uint32_t val  = shm->setValues;
                if (mask & (nwshared::kFbRaimOn | nwshared::kFbRaimV2)) {
                    const int m = (val & nwshared::kFbRaimOn)
                                      ? ((val & nwshared::kFbRaimV2) ? 2 : 1)
                                      : 0;
                    g_features.reverseAim.store(m);
                }
                if (mask & nwshared::kFbAntiAimless)
                    g_features.antiAimless.store((val & nwshared::kFbAntiAimless) != 0);
                if (mask & nwshared::kFbVisualRecoil)
                    g_features.visualRecoil.store((val & nwshared::kFbVisualRecoil) != 0);
                if (mask & nwshared::kFbAntiBhop)
                    g_features.antiBhop.store((val & nwshared::kFbAntiBhop) != 0);
                if (mask & nwshared::kFbGamesense)
                    g_features.gamesense.store((val & nwshared::kFbGamesense) != 0);
                if (mask & nwshared::kFbSpinSpeed) {
                    // Значение скорости закодировано в bits 8..15 команды.
                    const uint32_t s = (val >> 8) & 0xFFu;
                    if (s <= 10)
                        g_features.spinSpeed.store(static_cast<float>(s));
                }
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

        // --- 4a. Реверс аим (F1): raimv1 / raimv2. ---
        // Наводимся на ближайшего тиммейта всегда: стены не проверяются,
        // дальность не ограничена, живой предпочтительнее трупа. Если живых
        // нет — цель падает на ближайший труп тиммейта (пока он в списке).
        const int raimMode = g_features.reverseAim.load();
        if (raimMode != 0) {
            ent::Vector3 targetOrigin{};
            int targetHealth = 0;
            int aliveCount = 0, totalCount = 0;
            TeamScanStats stats;
            if (FindTeammateTarget(localPlayer, entityList, localTeam,
                                   targetOrigin, targetHealth,
                                   aliveCount, totalCount, stats)) {
                const ent::Vector3 eye = ent::GetEyePosition(localPlayer);
                if (eye.x != 0.0f || eye.y != 0.0f || eye.z != 0.0f) {
                    // +64 юнита вверх от origin — корпус/голова.
                    const ent::Vector3 target{ targetOrigin.x, targetOrigin.y,
                                               targetOrigin.z + 64.0f };
                    ent::Vector2 angles = ent::CalcAngles(eye, target);
                    ent::NormalizeAngles(angles.x, angles.y);

                    bool viaCmd = false;
                    if (raimMode == 2) {
                        viaCmd = Raimv2WriteToCmd(clientBase, angles.x, angles.y);
                        if (!viaCmd) {
                            ++g_cmdFails;
                            if (g_cmdFails >= 30) {
                                g_cmdFails = 0;
                                g_cmdLayout.resolved = false; // перепроба
                            }
                        }
                    }

                    // raimv1: только прямая запись. raimv2: юзеркоманда +
                    // подстраховка прямой записью (команда — главный канал,
                    // viewAngles — если игра уже применила кадр).
                    if (raimMode == 1 || !viaCmd) {
                        mem::Write<float>(viewAnglesPtr, angles.x);
                        mem::Write<float>(viewAnglesPtr + 4, angles.y);
                    }

                    static uint32_t lastLog = 0;
                    const uint32_t now = GetTickCount();
                    if (now - lastLog > 5000) {
                        lastLog = now;
                        NW_LOG(L"raimv%d: цель тиммейт (%.0f %.0f %.0f) hp=%d, eye (%.0f %.0f %.0f), углы (%.1f, %.1f)%s",
                               raimMode,
                               targetOrigin.x, targetOrigin.y, targetOrigin.z,
                               targetHealth,
                               eye.x, eye.y, eye.z,
                               angles.x, angles.y,
                               raimMode == 2 ? (viaCmd ? L" — канал user cmd" : L" — канал viewAngles") : L"");
                    }
                } else {
                    static uint32_t lastEye = 0;
                    const uint32_t now = GetTickCount();
                    if (now - lastEye > 5000) {
                        lastEye = now;
                        NW_LOG(L"raimv%d: глаза не прочитались (scene node / origin / viewOffset).",
                               raimMode);
                    }
                }
            } else {
                static uint32_t lastNone = 0;
                const uint32_t now = GetTickCount();
                if (now - lastNone > 5000) {
                    lastNone = now;
                    NW_LOG(L"raimv%d: живых тиммейтов нет. скан 512: павнов %d, врагов %d, тиммейтов %d (живых %d, нод %d, origin %d), localTeam=%d",
                           raimMode, stats.pawns, stats.enemies, stats.teammates,
                           aliveCount, stats.withNode, stats.withOrigin,
                           static_cast<int>(localTeam));
                }
            }
        }

        // --- 4b. Антиаимлесс (F2): виден враг — взгляд в пол. ---
        // Тот же метод, что работал до переноса: прямая запись viewAngles.
        if (g_features.antiAimless.load()) {
            bool enemySpotted = false;
            for (uint32_t i = 1; i < 512; ++i) {
                const uintptr_t pawn = ent::GetEntityByHandle(entityList, i);
                if (!pawn || pawn == localPlayer)
                    continue;

                const int enemyHealth = mem::Read<int>(pawn + off.m_iHealth);
                const uint8_t enemyTeam = ent::GetTeam(pawn);
                if (enemyHealth > 0 && enemyTeam != localTeam) {
                    enemySpotted = true;
                    break;
                }
            }

            if (enemySpotted) {
                const float spin = g_features.spinSpeed.load();
                const float curYaw = mem::Read<float>(viewAnglesPtr + 4);
                float newYaw = curYaw;
                // 0 — без кручения (только взгляд в пол), 1 — как раньше,
                // 10 — в десять раз быстрее.
                if (spin > 0.0f)
                    newYaw = curYaw + 15.0f * spin;
                if (newYaw > 180.0f) newYaw -= 360.0f;
                if (newYaw < -180.0f) newYaw += 360.0f;
                mem::Write<float>(viewAnglesPtr, 89.0f);
                mem::Write<float>(viewAnglesPtr + 4, newYaw);

                static bool logged = false;
                if (!logged) {
                    NW_LOG(L"F2: враг виден — камера в пол + кручение (скорость x%.0f).", spin);
                    logged = true;
                }
            }
        }
    }
}
