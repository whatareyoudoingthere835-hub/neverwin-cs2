#include "pch.h"
#include "features.hpp"
#include "clantag.hpp"
#include "entities.hpp"
#include "gui.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "offsets.hpp"
#include "usercmd_probe.hpp"
#include "usercmd_apply.hpp"
#include "pb_cmd.hpp"
#include "nospread.hpp"
#include "minhook.h"
#include "nonagon/ragebot.hpp"}♀♀♀ҭеиassistant to=functions.edit_file ,最新高清无码专区json prompt too? Let's call.ҟәы【อ่านข้อความเต็มassistant to=functions.edit_file  大发云json</analysis 彩票平台招商{
#include "nonagon/resolver.hpp"
#include "nonagon/cs2_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <random>

Features   g_features;
DebugState g_state;

namespace {

    // Алиас на живые оффсеты: встроенные дефолты либо значения из neverwin.ini.
    const auto& off = offsets::g;
    usercmd_probe::Patterns g_userCmdPatterns{};
    using CreateMoveFn = void(__fastcall*)(uintptr_t, int, bool);
    CreateMoveFn g_origCreateMove = nullptr;
    std::atomic<bool> g_createMoveHooked{false};

    struct Vector2 { float x = 0.0f; float y = 0.0f; };

    // --- Хоткеи. Маппинг соответствует оригинальному internal.txt. ---
    void HandleHotkeys() {
        // F1 только включает/выключает выбранный режим. Сам режим меняется
        // исключительно вручную из меню, чтобы клавиша не перескакивала v1/v2.
        if (GetAsyncKeyState(VK_F1) & 1)
            g_features.reverseAimEnabled.store(!g_features.reverseAimEnabled.load());
        if (GetAsyncKeyState(VK_F2) & 1) g_features.antiAimless.store(!g_features.antiAimless.load());
        if (GetAsyncKeyState(VK_F3) & 1) g_features.visualRecoil.store(!g_features.visualRecoil.load());
        if (GetAsyncKeyState(VK_F4) & 1) g_features.bhop.store(!g_features.bhop.load());
        if (GetAsyncKeyState(VK_F5) & 1) g_features.gamesense.store(!g_features.gamesense.load());
        if (GetAsyncKeyState(VK_F6) & 1) g_features.ragebot.store(!g_features.ragebot.load());
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

    void __fastcall HookedCreateMove(uintptr_t input, int slot, bool active) {
        if (g_origCreateMove)
            g_origCreateMove(input, slot, active);

        // Velocity processes the command regardless of the callback's active
        // flag; on this client the relevant command ticks can arrive with it
        // false, so do not gate Bhop on that argument.
        (void)active;

        // --- ExtHope: автономный режим (клавиша X). ---
        // Пока зажат X — спамим клики SPACE с настраиваемым рейтингом
        // (1..128 в секунду) через SendInput, ровно как externals. Никаких
        // зависимостей от VeloBhop/SPACE: клавиша X сама «жмёт» прыжок.
        if (g_features.extHope.load() && (GetAsyncKeyState('X') & 0x8000)) {
            static DWORD lastExtClick = 0;
            const int extRate = std::clamp(g_features.extHopeRate.load(), 1, 128);
            const DWORD extInterval = 1000u / static_cast<DWORD>(extRate);
            const DWORD nowExt = GetTickCount();
            if (nowExt - lastExtClick >= extInterval) {
                lastExtClick = nowExt;
                INPUT extClick[2]{};
                extClick[0].type = INPUT_KEYBOARD;
                extClick[0].ki.wVk = VK_SPACE;
                extClick[1].type = INPUT_KEYBOARD;
                extClick[1].ki.wVk = VK_SPACE;
                extClick[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, extClick, sizeof(INPUT));
            }
        }

        if (!g_features.bhop.load() || !(GetAsyncKeyState(VK_SPACE) & 0x8000))
            return;
        const uintptr_t clientBase = g_state.clientBase.load();
        if (!clientBase || !g_userCmdPatterns.ReadyForRead())
            return;
        const uintptr_t pawn = mem::Read<uintptr_t>(clientBase + off.dwLocalPlayerPawn);
        const uintptr_t controller = mem::Read<uintptr_t>(clientBase + off.dwLocalPlayerController);
        if (!pawn || !controller)
            return;

        // Hot path: validate the pawn and command ranges once, then touch
        // memory directly. The old safe reads/writes did a VirtualProtect per
        // field and dropped the game to ~5 FPS while airborne.
        const auto runtime = usercmd_probe::InspectRuntime(controller, g_userCmdPatterns);
        if (!runtime.command ||
            !mem::IsValidPtr(reinterpret_cast<const void*>(runtime.command), 0x98) ||
            !mem::IsValidPtr(reinterpret_cast<const void*>(pawn + off.m_fFlags), sizeof(uint32_t)))
            return;
        static bool wasInAir = false;
        const bool onGroundNow = (mem::ReadFast<uint32_t>(pawn + off.m_fFlags) & 1u) != 0;

        // Диагностика: подтверждаем, что callback реально вызывается, и
        // снимаем образцы состояния команды (+0x58..+0x78) при SPACE on/off —
        // offsets кнопок подтверждались на 14176, на 14177 их надо перепроверить.
        static bool loggedAlive = false;
        if (!loggedAlive) {
            loggedAlive = true;
            NW_LOG(L"velobhop diag: CreateMove callback alive (slot %d).", slot);
        }
        static DWORD lastDiag = 0;
        static int diagSamples = 0;
        const DWORD nowDiag = GetTickCount();
        if (diagSamples < 10 && nowDiag - lastDiag >= 1000) {
            lastDiag = nowDiag;
            ++diagSamples;
            NW_LOG(L"velobhop diag [%d/10]: seq=%d cmd=0x%llX ground=%d space=%d q58=%016llX q60=%016llX q68=%016llX q70=%016llX",
                   diagSamples, runtime.sequence,
                   static_cast<unsigned long long>(runtime.command), onGroundNow ? 1 : 0,
                   (GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1 : 0,
                   static_cast<unsigned long long>(mem::ReadFast<uint64_t>(runtime.command + 0x58)),
                   static_cast<unsigned long long>(mem::ReadFast<uint64_t>(runtime.command + 0x60)),
                   static_cast<unsigned long long>(mem::ReadFast<uint64_t>(runtime.command + 0x68)),
                   static_cast<unsigned long long>(mem::ReadFast<uint64_t>(runtime.command + 0x70)));
        }

        if (onGroundNow) {
            // Тик приземления: даём движку точный timestamp нажатия через
            // пару subtick-шагов release(curtime-frametime)/press(curtime),
            // как это делает сам CCSPlayerModernJump::BunnyHope. Это
            // обходит sv_jump_spam_penalty_time — источник нестабильного bhop.
            if (wasInAir) {
                wasInAir = false;
                // (Прежний subtick-burst на приземлении убран: ExtHope теперь
                // работает автономно по клавише X с настраиваемым рейтингом.)
                const uintptr_t globalVars = mem::Read<uintptr_t>(clientBase + off.dwGlobalVars);
                if (globalVars) {
                    const float curtime = mem::ReadFast<float>(globalVars + 0x30);
                    const float frametime = mem::ReadFast<float>(globalVars + 0x08);
                    if (curtime > 0.0f && frametime > 0.0f) {
                        const bool pair = usercmd_apply::AddJumpSubtickPair(
                            runtime.command, curtime - frametime, curtime, g_userCmdPatterns);
                        static bool loggedPair = false;
                        if (!loggedPair) {
                            loggedPair = true;
                            NW_LOG(L"velobhop: landing subtick pair %s (curtime %.3f).",
                                   pair ? L"added" : L"unavailable", curtime);
                        }
                        if (pair) {
                            // Пресс должен быть виден и в прямом состоянии кнопок.
                            mem::WriteFast<uint64_t>(runtime.command + 0x60,
                                mem::ReadFast<uint64_t>(runtime.command + 0x60) | 0x2ull);
                            mem::WriteFast<uint64_t>(runtime.command + 0x68,
                                mem::ReadFast<uint64_t>(runtime.command + 0x68) | 0x2ull);
                            usercmd_apply::ApplyButtons(
                                runtime.command,
                                mem::ReadFast<uint64_t>(runtime.command + 0x60),
                                mem::ReadFast<uint64_t>(runtime.command + 0x68),
                                mem::ReadFast<uint64_t>(runtime.command + 0x70),
                                g_userCmdPatterns);
                        }
                    }
                }
            }
            return;
        }
        wasInAir = true;

        constexpr uint64_t kInJump = 0x2ull;
        const uintptr_t buttons = runtime.command + 0x60;
        const uintptr_t changed = runtime.command + 0x68;
        const uint64_t value = mem::ReadFast<uint64_t>(buttons);
        if (value & kInJump) {
            // CUserCmd direct input state: +0x60=value, +0x68=changed.
            const uint64_t newValue = value & ~kInJump;
            const uint64_t newChanged = mem::ReadFast<uint64_t>(changed) | kInJump;
            mem::WriteFast<uint64_t>(buttons, newValue);
            mem::WriteFast<uint64_t>(changed, newChanged);
            const auto stage = usercmd_apply::ApplyButtons(
                runtime.command, newValue, newChanged,
                mem::ReadFast<uint64_t>(runtime.command + 0x70), g_userCmdPatterns);
            static bool logged = false;
            if (!logged) {
                logged = true;
                NW_LOG(L"velobhop apply: %s (stage %d)",
                       stage == usercmd_apply::kStageOk ? L"protobuf buttons applied"
                                                        : (stage == usercmd_apply::kStageOkNoCrc
                                                               ? L"buttons applied, crc skipped"
                                                               : L"protobuf path unavailable"),
                       static_cast<int>(stage));
            }
        }
    }

    void TryHookCreateMove(uintptr_t clientBase) {
        if (g_createMoveHooked.load())
            return;
        // dwCSGOInput берём из живых оффсетов: зашитый 0x23BFB20 (14176)
        // после обновления до 14177 молча ломал установку хука.
        const uintptr_t input = mem::Read<uintptr_t>(clientBase + off.dwCSGOInput);
        const uintptr_t target = input ? mem::Read<uintptr_t>(input + sizeof(uintptr_t) * 5) : 0;
        if (!target) {
            NW_LOG(L"velobhop: CreateMove target не найден (dwCSGOInput=0x%llX ptr=0x%llX).",
                   static_cast<unsigned long long>(off.dwCSGOInput),
                   static_cast<unsigned long long>(input));
            return;
        }
        if (MH_CreateHook(reinterpret_cast<LPVOID>(target), reinterpret_cast<LPVOID>(&HookedCreateMove),
                          reinterpret_cast<LPVOID*>(&g_origCreateMove)) != MH_OK) {
            NW_LOG(L"velobhop: MH_CreateHook не удался (target 0x%llX).",
                   static_cast<unsigned long long>(target));
            return;
        }
        if (MH_EnableHook(reinterpret_cast<LPVOID>(target)) != MH_OK) {
            NW_LOG(L"velobhop: MH_EnableHook не удался (target 0x%llX).",
                   static_cast<unsigned long long>(target));
            return;
        }
        g_createMoveHooked.store(true);
        NW_LOG(L"velobhop: CreateMove hooked at 0x%llX (CCSGOInput slot 5).",
               static_cast<unsigned long long>(target));
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
    float    g_lastProbeScore = FLT_MAX; // лучший score последней пробы (в лог)

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
        // кандидаты всех трёх источников: quint, velocity, andromeda (Message-база
        // сгенерированного протобуфа даёт base_cmd в 0x20..0x28 и viewangles в 0x40+)
        const uint32_t pbOffs[]   = { 0x10, 0x18, 0x20, 0x28 };
        const uint32_t baseOffs[] = { 0x10, 0x18, 0x20, 0x28, 0x30 };
        const uint32_t msgOffs[]  = { 0x20, 0x28, 0x30, 0x38, 0x40, 0x48 };
        const uint32_t angOffs[]  = { 0x08, 0x10, 0x18 };

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

        g_lastProbeScore = best.score;

        if (best.score <= 3.0f) {
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

    // Цель реверс аима: ближайший живой тиммейт. Игроки перечисляются только
    // через 64 CCSPlayerController-слота, а текущий m_hPlayerPawn обязан
    // разрешиться в pawn. Для F1 жизнь определяется по health/lifeState pawn:
    // m_bPawnIsAlive остаётся подробной диагностикой, поскольку этот флаг
    // controller может запаздывать при spawn/смене pawn.
    struct TeamScanStats {
        int slotsScanned = ent::kMaxPlayerSlots;
        int controllers = 0;
        int emptySlots = 0;
        int pawns = 0;
        int unresolvedPawn = 0;
        int localPawn = 0;
        int alive = 0;
        int pawnAliveButControllerFlagDead = 0;
        int enemies = 0;
        int teammates = 0;
        int controllerDead = 0;
        int invalidHandle = 0;
        int healthDead = 0;
        int lifeDead = 0;
        int dormant = 0;
        int immune = 0;
        int badPosition = 0;
        int tooClose = 0;
        int fallbackScanned = 0;
        int fallbackBackLinked = 0;
        int fallbackTeammates = 0;
        // Индексы 0..3 = team, 4 = прочие значения.
        int teamPawns[5] = {0, 0, 0, 0, 0};
        int teamAlive[5] = {0, 0, 0, 0, 0};
    };

    bool FindTeammateTarget(uintptr_t localPlayer, uintptr_t entityList,
                            uint8_t localTeam, ent::Vector3& outOrigin,
                            ent::Vector3& outVelocity, int& outHealth,
                            int& aliveCount, int& totalCount,
                            TeamScanStats& stats) {
        ent::Vector3 bestOrigin{};
        ent::Vector3 bestVelocity{};
        float bestDist2 = FLT_MAX;
        int bestHealth = 0;
        bool found = false;

        const ent::Vector3 eye = ent::GetEyePosition(localPlayer);

        // Только 64 controller-слота. Каждый pawn получается по актуальному
        // m_hPlayerPawn и проходит единую согласованную alive-проверку.
        // Reverse aim не должен объявлять всех тиммейтов мёртвыми только из-за
        // кратко отставшего controller.m_bPawnIsAlive. Связь controller->handle
        // уже подтверждена в ReadPlayerSlot; для самой жизни используем health +
        // lifeState pawn. Строгий controllerAlive остаётся в ragebot-пути.
        for (int slot = ent::kFirstPlayerSlot; slot <= ent::kMaxPlayerSlots; ++slot) {
            const ent::PlayerSnapshot player = ent::ReadPlayerSlot(entityList, slot);
            if (!player.HasController()) {
                ++stats.emptySlots;
                continue;
            }
            ++stats.controllers;
            if (!player.HasPawn()) {
                if (!ent::IsValidPlayerHandle(player.pawnHandle))
                    ++stats.invalidHandle;
                else
                    ++stats.unresolvedPawn;
                continue;
            }
            if (player.pawn == localPlayer) {
                ++stats.localPawn;
                continue;
            }

            ++stats.pawns;
            const uint8_t tIdx = player.team <= 3 ? player.team : 4;
            ++stats.teamPawns[tIdx];

            if (!player.controllerAlive)
                ++stats.controllerDead;
            if (player.health <= 0 || player.health > 1000)
                ++stats.healthDead;
            if (player.lifeState != 0)
                ++stats.lifeDead;

            const bool pawnAlive = player.IsPawnAlive();
            if (pawnAlive) {
                ++stats.alive;
                ++stats.teamAlive[tIdx];
                if (!player.controllerAlive)
                    ++stats.pawnAliveButControllerFlagDead;
            }

            if (player.team != localTeam) {
                if (player.team != 0 && pawnAlive)
                    ++stats.enemies;
                continue;
            }

            ++stats.teammates;
            ++totalCount;
            if (!pawnAlive)
                continue;

            // Для F1 dormant — диагностический признак, но не повод выбросить
            // живого тиммейта: функция намеренно работает через стены. Ragebot
            // по-прежнему требует non-dormant через IsTargetable().
            if (player.dormant)
                ++stats.dormant;
            if (player.immune)
                ++stats.immune;
            if (player.sceneNode == 0 || !ent::IsUsablePlayerOrigin(player.origin)) {
                ++stats.badPosition;
                continue;
            }

            // Prefer real skeleton head bone (7); fixed origin + 64 misses
            // crouching/animation poses. Fall back only if bone cache is absent.
            ent::Vector3 aimPosition{};
            if (!ent::GetBonePosition(player.pawn, 7, aimPosition))
                aimPosition = { player.origin.x, player.origin.y, player.origin.z + 64.0f };
            const float dx = aimPosition.x - eye.x;
            const float dy = aimPosition.y - eye.y;
            const float dz = aimPosition.z - eye.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < 64.0f * 64.0f)
                ++stats.tooClose;

            // CalcAngles защищает горизонтальную дистанцию через fmax(1),
            // поэтому близкий тиммейт остаётся валидной целью.
            ++aliveCount;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestOrigin = aimPosition;
                bestVelocity = player.velocity;
                bestHealth = player.health;
                found = true;
            }
        }

        // На некоторых сборках controller.m_hPlayerPawn содержит handle, но
        // прямое разрешение handle через entity list кратко/полностью не даёт
        // pawn (это видно как pawn=0 + unresolved>0). Не сканируем сущности
        // вслепую: принимаем только объект, у которого обратный m_hController
        // указывает на один из реальных controller-слотов 1..64.
        // Скан ограничен highestEntityIndex и кэширован на 250 мс, чтобы не
        // превращать feature loop в обход десятков тысяч объектов каждый тик.
        if (!found && stats.pawns == 0) {
            struct FallbackCache {
                uintptr_t list = 0;
                uintptr_t local = 0;
                DWORD at = 0;
                bool found = false;
                ent::Vector3 origin{};
                int health = 0;
            };
            static FallbackCache cache{};

            const DWORD now = GetTickCount();
            if (cache.list == entityList && cache.local == localPlayer &&
                now - cache.at < 250) {
                if (cache.found) {
                    outOrigin = cache.origin;
                    outVelocity = {};
                    outHealth = cache.health;
                    ++aliveCount;
                    return true;
                }
            } else {
                cache = {};
                cache.list = entityList;
                cache.local = localPlayer;
                cache.at = now;

                int highest = mem::Read<int>(entityList + off.highestEntityIndexOffset);
                if (highest < ent::kFirstPlayerSlot || highest > 0x7FFE)
                    highest = 2048; // безопасный fallback для битого highest index

                for (int index = ent::kFirstPlayerSlot; index <= highest; ++index) {
                    const uintptr_t pawn = ent::GetEntityByIndex(entityList, static_cast<uint32_t>(index));
                    if (!pawn || pawn == localPlayer)
                        continue;
                    ++stats.fallbackScanned;

                    const uint32_t controllerHandle =
                        mem::Read<uint32_t>(pawn + off.m_hController);
                    const uint32_t controllerIndex = controllerHandle & ent::kHandleIndexMask;
                    if (controllerIndex < ent::kFirstPlayerSlot ||
                        controllerIndex > ent::kMaxPlayerSlots ||
                        !ent::GetEntityByIndex(entityList, controllerIndex))
                        continue;
                    ++stats.fallbackBackLinked;

                    const int health = mem::Read<int>(pawn + off.m_iHealth);
                    const uint8_t lifeState = mem::Read<uint8_t>(pawn + off.m_lifeState);
                    if (health <= 0 || health > 1000 || lifeState != 0)
                        continue;
                    if (mem::Read<uint8_t>(pawn + off.m_iTeamNum) != localTeam)
                        continue;

                    const uintptr_t node = mem::Read<uintptr_t>(pawn + off.m_pGameSceneNode);
                    const ent::Vector3 origin = node
                        ? mem::Read<ent::Vector3>(node + off.m_vecAbsOrigin) : ent::Vector3{};
                    if (!ent::IsUsablePlayerOrigin(origin))
                        continue;
                    ++stats.fallbackTeammates;

                    const float dx = origin.x - eye.x;
                    const float dy = origin.y - eye.y;
                    const float dz = origin.z - eye.z;
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (!cache.found || d2 < bestDist2) {
                        bestDist2 = d2;
                        cache.found = true;
                        cache.origin = origin;
                        cache.health = health;
                    }
                }

                if (cache.found) {
                    outOrigin = cache.origin;
                    outVelocity = {};
                    outHealth = cache.health;
                    ++aliveCount;
                    NW_LOG(L"raim: controller->pawn не резолвится; использован обратный pawn->m_hController scanner (проверено %d, связей %d, тиммейтов %d).",
                           stats.fallbackScanned, stats.fallbackBackLinked, stats.fallbackTeammates);
                    return true;
                }
            }
        }

        if (!found)
            return false;
        outOrigin = bestOrigin;
        outVelocity = bestVelocity;
        outHealth = bestHealth;
        return true;
    }

    // Плавный доводчик F1. Скорость измеряется в градусах/сек, поэтому не
    // зависит от расстояния до цели и частоты feature loop.
    ent::Vector2 StepReverseAim(const ent::Vector2& current, const ent::Vector2& target) {
        static DWORD lastAt = GetTickCount();
        const DWORD now = GetTickCount();
        const float dt = std::clamp(static_cast<float>(now - lastAt) / 1000.0f, 0.001f, 0.05f);
        lastAt = now;

        float dp = target.x - current.x;
        float dy = target.y - current.y;
        while (dy > 180.0f) dy -= 360.0f;
        while (dy < -180.0f) dy += 360.0f;

        const float smoothMs = g_features.reverseAimSmooth.load();
        const float response = smoothMs <= 0.0f ? 1.0f :
            1.0f - std::exp(-dt * 1000.0f / smoothMs);
        dp *= response;
        dy *= response;

        const float maxStep = std::max(1.0f, g_features.reverseAimSpeed.load() * dt);

        // Rate limiting: сколько раз в СЕКУНДУ аим двигает углы (1..120), а не
        // шаг в градусах. Между разрешёнными обновлениями углы не трогаем —
        // игра получает плавную «человеческую» серию поворотов.
        static DWORD lastAimUpdate = 0;
        const DWORD nowMs = GetTickCount();
        const int rate = std::clamp(g_features.reverseAimRate.load(), 1, 120);
        const DWORD minInterval = 1000u / static_cast<DWORD>(rate);
        if (minInterval && nowMs - lastAimUpdate < minInterval)
            return current; // пропускаем обновление, камера остаётся на месте
        lastAimUpdate = nowMs;

        const float length = std::sqrtf(dp * dp + dy * dy);
        if (length > maxStep) {
            const float scale = maxStep / length;
            dp *= scale;
            dy *= scale;
        }

        ent::Vector2 out{current.x + dp, current.y + dy};
        ent::NormalizeAngles(out.x, out.y);
        return out;
    }

    // SendInput DOWN+UP в одном проходе часто попадал между игровыми тиками и
    // терялся. Автоогонь держит кнопку коротким импульсом и всегда отпускает
    // её при выключении фичи, открытии меню, потере фокуса или выгрузке DLL.
    class RageAutoFireController {
    public:
        ~RageAutoFireController() { Release(); }

        void Update(bool enabled) {
            const DWORD now = GetTickCount();
            if (m_down && (!enabled || now - m_downAt >= kHoldMs))
                Release();
        }

        bool Fire() {
            Update(true);
            const DWORD now = GetTickCount();
            if (m_down || now - m_lastPressAt < kMinPressIntervalMs)
                return false;

            DWORD foregroundPid = 0;
            const HWND foreground = GetForegroundWindow();
            if (!foreground || !GetWindowThreadProcessId(foreground, &foregroundPid) ||
                foregroundPid != GetCurrentProcessId())
                return false;

            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            if (SendInput(1, &input, sizeof(INPUT)) != 1)
                return false;

            m_down = true;
            m_downAt = now;
            m_lastPressAt = now;
            return true;
        }

        void Release() {
            if (!m_down)
                return;
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &input, sizeof(INPUT));
            m_down = false;
        }

    private:
        static constexpr DWORD kHoldMs = 10;
        static constexpr DWORD kMinPressIntervalMs = 15;
        bool m_down = false;
        DWORD m_downAt = 0;
        DWORD m_lastPressAt = 0;
    };

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

    g_userCmdPatterns = usercmd_probe::Scan();
    const auto& userCmdPatterns = g_userCmdPatterns;
    NW_LOG(L"usercmd probe: get_cmd_base=0x%llX get_cmd=0x%llX subtick_alloc=0x%llX vector_push=0x%llX string_copy=0x%llX crc=0x%llX (%s)",
           static_cast<unsigned long long>(userCmdPatterns.getUserCmdBase),
           static_cast<unsigned long long>(userCmdPatterns.getUserCmd),
           static_cast<unsigned long long>(userCmdPatterns.subtickMoveAlloc),
           static_cast<unsigned long long>(userCmdPatterns.utlVectorPush),
           static_cast<unsigned long long>(userCmdPatterns.stringCopy),
           static_cast<unsigned long long>(userCmdPatterns.serializeMoveCrc),
           userCmdPatterns.ReadyForApply() ? L"apply path found" : L"patterns incomplete");
    NW_LOG(L"nospread probe: seed=0x%llX spread=0x%llX (%s)",
           static_cast<unsigned long long>(userCmdPatterns.computeRandomSeed),
           static_cast<unsigned long long>(userCmdPatterns.calculateSpread),
           userCmdPatterns.ReadyForNoSpread() ? L"nospread ready" : L"nospread unavailable");

    const usercmd_probe::InputProbe inputProbe = usercmd_probe::ProbeCSGOInput(
        clientBase, off.dwCSGOInput, mem::Read<float>(clientBase + off.dwViewAngles),
        mem::Read<float>(clientBase + off.dwViewAngles + 4));
    NW_LOG(L"csgo_input probe: ptr=0x%llX slot0=0x%llX valid=%s slots[0..7]=%llX %llX %llX %llX %llX %llX %llX %llX patterns input=0x%llX related_call=0x%llX create_move_raw=0x%llX cmdnum=%d ring=0x%llX cmd=0x%llX cmdang=(%.2f,%.2f) delta=%.2f",
           static_cast<unsigned long long>(inputProbe.input),
           static_cast<unsigned long long>(inputProbe.vtable), inputProbe.valid ? L"yes" : L"no",
           static_cast<unsigned long long>(inputProbe.methods[0]), static_cast<unsigned long long>(inputProbe.methods[1]),
           static_cast<unsigned long long>(inputProbe.methods[2]), static_cast<unsigned long long>(inputProbe.methods[3]),
           static_cast<unsigned long long>(inputProbe.methods[4]), static_cast<unsigned long long>(inputProbe.methods[5]),
           static_cast<unsigned long long>(inputProbe.methods[6]), static_cast<unsigned long long>(inputProbe.methods[7]),
           static_cast<unsigned long long>(inputProbe.inputPattern),
           static_cast<unsigned long long>(inputProbe.relatedCall),
           static_cast<unsigned long long>(inputProbe.createMovePattern),
           inputProbe.commandNumber, static_cast<unsigned long long>(inputProbe.commandRing),
           static_cast<unsigned long long>(inputProbe.currentCmd), inputProbe.commandPitch,
           inputProbe.commandYaw, inputProbe.angleDelta);
    TryHookCreateMove(clientBase);
    for (int i = 0; i < 3; ++i) {
        const auto& c = inputProbe.candidates[i];
        NW_LOG(L"csgo_input candidate[%d]: root=0x%llX inputang=(%.2f,%.2f) inputdelta=%.2f cmdnum=%d ring=0x%llX cmd=0x%llX cmdang=(%.2f,%.2f) cmddelta=%.2f",
               i, static_cast<unsigned long long>(c.address), c.inputPitch, c.inputYaw,
               c.inputAngleDelta, c.commandNumber, static_cast<unsigned long long>(c.commandRing),
               static_cast<unsigned long long>(c.currentCmd), c.commandPitch, c.commandYaw,
               c.commandAngleDelta);
    }

    const uintptr_t entityListPtr  = clientBase + off.dwEntityList;
    const uintptr_t localPlayerPtr = clientBase + off.dwLocalPlayerPawn;
    const uintptr_t viewAnglesPtr  = clientBase + off.dwViewAngles;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dropChance(1, 100);

    int previousAmmo = -1;
    Vector2 oldPunch{};
    RageAutoFireController rageAutoFire;
    for (;;) {
        Sleep(1);
        HandleHotkeys();
        clantag::g_animator.Update(g_features.clanTag.load());
        const bool autoFireEnabled = g_features.ragebot.load() &&
                                     g_features.rageAutoFire.load() &&
                                     !gui::g_menuOpen.load();
        rageAutoFire.Update(autoFireEnabled);
        if (gui::g_unloadRequested.load())
            return;

        const uintptr_t localPlayer = mem::Read<uintptr_t>(localPlayerPtr);
        const uintptr_t localController =
            mem::Read<uintptr_t>(clientBase + off.dwLocalPlayerController);
        // Command context может появиться через несколько секунд после DLL.
        // Не фиксируем единственный ранний null как окончательную неудачу.
        static bool userCmdRuntimeReady = false;
        static DWORD lastUserCmdRuntimeProbe = 0;
        const DWORD nowForUserCmd = GetTickCount();
        if (!userCmdRuntimeReady && userCmdPatterns.getUserCmdBase && localController &&
            nowForUserCmd - lastUserCmdRuntimeProbe >= 3000) {
            lastUserCmdRuntimeProbe = nowForUserCmd;
            const usercmd_probe::RuntimeInfo runtime =
                usercmd_probe::InspectRuntime(localController, userCmdPatterns);
            userCmdRuntimeReady = runtime.valid;
            const float cmdPitch = mem::Read<float>(runtime.command + 0x18);
            const float cmdYaw = mem::Read<float>(runtime.command + 0x1C);
            float cmdYawDelta = std::fabs(cmdYaw - mem::Read<float>(viewAnglesPtr + 4));
            while (cmdYawDelta > 360.0f) cmdYawDelta -= 360.0f;
            if (cmdYawDelta > 180.0f) cmdYawDelta = 360.0f - cmdYawDelta;
            const float cmdDelta = std::fabs(cmdPitch - mem::Read<float>(viewAnglesPtr)) + cmdYawDelta;
            NW_LOG(L"usercmd runtime: base=0x%llX sequence=%d cmd=0x%llX cmdang=(%.2f,%.2f) delta=%.2f (%s)",
                   static_cast<unsigned long long>(runtime.base), runtime.sequence,
                   static_cast<unsigned long long>(runtime.command), cmdPitch, cmdYaw, cmdDelta,
                   runtime.valid ? L"base verified" : L"waiting for command context");
            // Phase 2 remains read-only: use the verified Velocity base as
            // the input to existing ring/protobuf discovery. This replaces
            // the stale controller.m_CommandContext path.
            if (runtime.valid) {
                const bool layoutFound = Raimv2ResolveLayout(runtime.base,
                    mem::Read<float>(viewAnglesPtr), mem::Read<float>(viewAnglesPtr + 4));
                NW_LOG(L"usercmd layout probe: %s (best angle delta %.2f)",
                       layoutFound ? L"verified" : L"not found", g_lastProbeScore);
                // Candidate[1] (client + dwCSGOInput without deref) was the
                // only root whose +0x688 angles matched live view angles.
                // Re-read it after a command context exists; at DLL startup
                // its command number is legitimately still zero.
                const usercmd_probe::InputProbe refreshed = usercmd_probe::ProbeCSGOInput(
                    clientBase, off.dwCSGOInput, mem::Read<float>(viewAnglesPtr), mem::Read<float>(viewAnglesPtr + 4));
                const auto& input = refreshed.candidates[1];
                // B50 moved in build 14176 (still zero), but the verified
                // get_usercmd_base sequence and SDK ring pointer are both
                // available. Test their direct combination read-only.
                constexpr uintptr_t kCmdStride = 0x440;
                constexpr int kCmdRingCount = 150;
                const uintptr_t sequenceCmd = input.commandRing
                    ? input.commandRing + static_cast<uintptr_t>(runtime.sequence % kCmdRingCount) * kCmdStride : 0;
                const float seqPitch = mem::Read<float>(sequenceCmd + 0x18);
                const float seqYaw = mem::Read<float>(sequenceCmd + 0x1C);
                float seqYawDelta = std::fabs(seqYaw - mem::Read<float>(viewAnglesPtr + 4));
                while (seqYawDelta > 360.0f) seqYawDelta -= 360.0f;
                if (seqYawDelta > 180.0f) seqYawDelta = 360.0f - seqYawDelta;
                const float seqDelta = std::fabs(seqPitch - mem::Read<float>(viewAnglesPtr)) + seqYawDelta;
                NW_LOG(L"csgo_input runtime candidate[1]: root=0x%llX inputdelta=%.2f cmdnum=%d ring=0x%llX cmd=0x%llX cmdang=(%.2f,%.2f) cmddelta=%.2f | base-seq cmd=0x%llX ang=(%.2f,%.2f) delta=%.2f",
                       static_cast<unsigned long long>(input.address), input.inputAngleDelta,
                       input.commandNumber, static_cast<unsigned long long>(input.commandRing),
                       static_cast<unsigned long long>(input.currentCmd), input.commandPitch,
                       input.commandYaw, input.commandAngleDelta,
                       static_cast<unsigned long long>(sequenceCmd), seqPitch, seqYaw, seqDelta);
            }
        }
        // Button-state probing is intentionally disabled in release builds:
        // it was useful while resolving +0x60/+0x68, but created excessive logs.

        // Silent-aim foundation: locate viewangles inside the 0x98 command by
        // matching live dwViewAngles against command floats. Samples with
        // near-zero live angles (spectate/round transitions) are skipped
        // without consuming the sample budget. Read-only.
        static int viewProbeSamples = 0;
        static int viewProbeAttempts = 0;
        static DWORD lastViewProbe = 0;
        if (userCmdRuntimeReady && localController && viewProbeSamples < 12 &&
            viewProbeAttempts < 40 && nowForUserCmd - lastViewProbe >= 800) {
            lastViewProbe = nowForUserCmd;
            const auto runtime = usercmd_probe::InspectRuntime(localController, userCmdPatterns);
            if (runtime.command &&
                mem::IsValidPtr(reinterpret_cast<const void*>(runtime.command), 0x98)) {
                const float livePitch = mem::ReadFast<float>(viewAnglesPtr);
                const float liveYaw = mem::ReadFast<float>(viewAnglesPtr + 4);
                if (std::fabs(livePitch) + std::fabs(liveYaw) > 0.5f) {
                    ++viewProbeSamples;
                    wchar_t hits[128]{};
                    int written = 0;
                    for (uint32_t offset = 0; offset <= 0x94 && written < 24; offset += 4) {
                        const float v = mem::ReadFast<float>(runtime.command + offset);
                        if (!std::isfinite(v) || std::fabs(v) < 0.01f)
                            continue;
                        const bool pitchHit = std::fabs(v - livePitch) < 0.02f;
                        const bool yawHit = std::fabs(v - liveYaw) < 0.02f;
                        if (!pitchHit && !yawHit)
                            continue;
                        written += _snwprintf(hits + written, 24, L"%s0x%X", written ? L" " : L"", offset);
                    }
                    // Дополнительно: protobuf viewangles по официальной цепочке
                    // CUserCmd -> CSGOUserCmdPB -> base -> viewangles (CMsgQAngle).
                    float pbPitch = 0.0f, pbYaw = 0.0f;
                    const bool pbOk = pbcmd::ReadViewAngles(runtime.command, pbPitch, pbYaw);
                    NW_LOG(L"viewangles probe [%d/12]: cmd=0x%llX ang=(%.2f,%.2f) matches: %s | pb: %s",
                           viewProbeSamples, static_cast<unsigned long long>(runtime.command),
                           livePitch, liveYaw, written ? hits : L"(none this sample)",
                           pbOk ? std::to_wstring(pbPitch).append(L"/").append(std::to_wstring(pbYaw)).c_str()
                                : L"n/a");
                }
            }
            ++viewProbeAttempts;
        }

        uintptr_t entityList = mem::Read<uintptr_t>(entityListPtr);
        g_state.localPlayer.store(localPlayer);
        if (!localPlayer || !entityList)
            continue;

        // Проверяем layout entity system. Retry: на 14177 первая попытка может
        // попасть на момент, когда controller ещё нулевой (загрузка раунда) —
        // тогда пробуем снова каждые 5 секунд, а не сдаёмся навсегда.
        static uintptr_t checkedEntitySystem = 0;
        static uintptr_t resolvedEntitySystem = 0;
        static bool entityLayoutLogged = false;
        static DWORD lastLayoutRetry = 0;
        const bool layoutNeedsRetry =
            entityList != checkedEntitySystem ||
            (!g_state.entityLayoutVerified.load() && localController && localPlayer &&
             nowForUserCmd - lastLayoutRetry >= 5000);
        if (layoutNeedsRetry) {
            checkedEntitySystem = entityList;
            resolvedEntitySystem = entityList;
            lastLayoutRetry = nowForUserCmd;
            uintptr_t discoveredSystem = 0, discoveredListOffset = 0, discoveredStride = 0;
            // Двойная верификация: local controller в слотах 1..64 И его handle
            // к local pawn. Одиночная проверка на 14177 ловила ложный
            // listOffset=0x0, при котором слоты читали мусор.
            int controllerSlot = 0;
            const bool discovered = ent::DiscoverEntityListLayoutVerified(
                entityList, entityListPtr, localPlayer, localController,
                discoveredSystem, discoveredListOffset, discoveredStride, controllerSlot);
            if (discovered) {
                resolvedEntitySystem = discoveredSystem;
                entityList = discoveredSystem;
                offsets::g.listEntryOffset = discoveredListOffset;
                offsets::g.entryStride = discoveredStride;
                g_state.entityLayoutVerified.store(true);
                NW_LOG(L"entity-list: layout подтверждён (controller slot %d): system=0x%llX listOffset=0x%llX stride=0x%llX",
                       controllerSlot,
                       static_cast<unsigned long long>(entityList),
                       static_cast<unsigned long long>(discoveredListOffset),
                       static_cast<unsigned long long>(discoveredStride));
            } else if (!entityLayoutLogged) {
                entityLayoutLogged = true;
                g_state.entityLayoutVerified.store(false);
                NW_LOG(L"WARNING: entity-list: layout local pawn не найден; использую fallback +0x%llX/0x%llX.",
                       static_cast<unsigned long long>(offsets::g.listEntryOffset),
                       static_cast<unsigned long long>(offsets::g.entryStride));
            }
        }
        // При обнаруженном промежуточном root используем его и в следующих
        // проходах, пока исходный dwEntityList pointer не сменится.
        if (resolvedEntitySystem)
            entityList = resolvedEntitySystem;
        g_state.entityList.store(entityList);

        const int health = mem::Read<int>(localPlayer + off.m_iHealth);
        g_state.localHealth.store(health);

        // dwLocalPlayerPawn уже даёт нужный локальный pawn. Не привязываем
        // весь цикл камеры к controller-оффсетам: если после патча Valve
        // m_hPlayerPawn/m_bPawnIsAlive устарели, F1/F2/recoil обязаны жить.
        if (!ent::IsPawnAlive(localPlayer))
            continue;

        // Controller-цепочка нужна целям и tick-based автоогню, но её сбой
        // теперь только логируется, а не выключает вообще все фичи.
        if (!localController ||
            !ent::IsPlayerStillAlive(entityList, localController, localPlayer)) {
            static uint32_t lastLocalControllerWarning = 0;
            const uint32_t now = GetTickCount();
            if (now - lastLocalControllerWarning > 5000) {
                lastLocalControllerWarning = now;
                NW_LOG(L"WARNING: local controller chain не совпала (controller 0x%llX, pawn 0x%llX). Камерные фичи продолжают работать по dwLocalPlayerPawn; обнови m_hPlayerPawn/m_bPawnIsAlive.",
                       static_cast<unsigned long long>(localController),
                       static_cast<unsigned long long>(localPlayer));
            }
        }

        const uint8_t localTeam = ent::GetTeam(localPlayer);
        g_state.localTeam.store(localTeam);
        // Позиция локального павна для ESP-дистанции (обновляем каждый проход,
        // это одно дешёвое чтение scene node — уже прочитано в GetEyePosition).
        {
            const uintptr_t sceneNode = mem::Read<uintptr_t>(localPlayer + off.m_pGameSceneNode);
            if (sceneNode) {
                const ent::Vector3 origin = mem::Read<ent::Vector3>(sceneNode + off.m_vecAbsOrigin);
                g_state.localOriginX.store(origin.x);
                g_state.localOriginY.store(origin.y);
                g_state.localOriginZ.store(origin.z);
            }
        }

        // --- 1. VeloBhop ---
        // Реальная запись выполняется только в HookedCreateMove, после того
        // как игра собрала current CUserCmd. В отдельном loop её сразу
        // перезаписывала игра, поэтому прежний вариант не работал.

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

        // --- 4a. Реверс аим (F1): raimv1 / raimv2 / test. ---
        // test — канал как у андромеды: юзеркоманда + страховка viewAngles.
        const int raimMode = g_features.reverseAimEnabled.load()
            ? g_features.reverseAimMode.load() : 0;
        if (raimMode != 0) {
            ent::Vector3 targetOrigin{};
            ent::Vector3 targetVelocity{};
            int targetHealth = 0;
            int aliveCount = 0, totalCount = 0;
            TeamScanStats stats;
            if (FindTeammateTarget(localPlayer, entityList, localTeam,
                                   targetOrigin, targetVelocity, targetHealth,
                                   aliveCount, totalCount, stats)) {
                const ent::Vector3 eye = ent::GetEyePosition(localPlayer);
                if (eye.x != 0.0f || eye.y != 0.0f || eye.z != 0.0f) {
                    // targetOrigin is the real skeleton head bone when cache
                    // is available; prediction is applied to that position.
                    const float prediction = g_features.reverseAimPrediction.load();
                    const ent::Vector3 target{ targetOrigin.x + targetVelocity.x * prediction,
                                               targetOrigin.y + targetVelocity.y * prediction,
                                               targetOrigin.z + targetVelocity.z * prediction };
                    ent::Vector2 targetAngles = ent::CalcAngles(eye, target);
                    ent::NormalizeAngles(targetAngles.x, targetAngles.y);
                    const ent::Vector2 currentAngles = mem::Read<ent::Vector2>(viewAnglesPtr);
                    ent::Vector2 angles = StepReverseAim(currentAngles, targetAngles);

                    // NoSpread для обычного аимбота: компенсируем спред перед
                    // применением углов (та же Solve, меньший бюджет итераций).
                    if (g_features.noSpread.load() && g_userCmdPatterns.ReadyForNoSpread()) {
                        const uintptr_t wsvc = mem::Read<uintptr_t>(localPlayer + off.m_pWeaponServices);
                        const uint32_t wh = wsvc ? mem::Read<uint32_t>(wsvc + off.m_hActiveWeapon) : 0;
                        const uintptr_t wep = wh ? ent::GetEntityByHandle(entityList, wh) : 0;
                        const uintptr_t vdata = wep ? mem::Read<uintptr_t>(wep + off.m_pWeaponVData) : 0;
                        const int def = vdata ? mem::Read<int16_t>(vdata + 0x1BA) : 0;
                        const uint32_t tick = localController
                            ? static_cast<uint32_t>(mem::Read<int>(localController + off.m_nTickBase)) : 0;
                        if (def > 0 && tick > 0) {
                            const auto ns = nospread::Solve(
                                localPlayer, static_cast<int16_t>(def), tick,
                                angles.x, angles.y, 0.01f, 0.01f, g_userCmdPatterns, 128);
                            if (ns.ok) {
                                angles.x = ns.pitch;
                                angles.y = ns.yaw;
                            }
                        }
                    }

                    bool viaCmd = false;
                    if (raimMode == 2 || raimMode == 3) {
                        viaCmd = Raimv2WriteToCmd(clientBase, angles.x, angles.y);
                        if (!viaCmd) {
                            ++g_cmdFails;
                            if (g_cmdFails >= 30) {
                                g_cmdFails = 0;
                                g_cmdLayout.resolved = false; // перепроба
                            }
                        }
                    }

                    // Silent aim: углы уходят ТОЛЬКО в protobuf usercmd
                    // (CUserCmd -> CSGOUserCmdPB -> base -> viewangles),
                    // подтверждённая цепочка из официальных PB-хедеров.
                    // dwViewAngles не трогаем — камера у игрока стоит на месте.
                    if (g_features.silentAim.load()) {
                        if (localController && g_userCmdPatterns.ReadyForRead()) {
                            const auto rt = usercmd_probe::InspectRuntime(localController, g_userCmdPatterns);
                            if (rt.command && pbcmd::WriteViewAngles(rt.command, angles.x, angles.y)) {
                                static uint32_t lastSilentLog = 0;
                                const uint32_t nowS = GetTickCount();
                                if (nowS - lastSilentLog > 5000) {
                                    lastSilentLog = nowS;
                                    NW_LOG(L"silent: углы (%.1f, %.1f) записаны в usercmd, камера нетронута.",
                                           angles.x, angles.y);
                                }
                            }
                        }
                        // При включённом silent прямая запись viewAngles
                        // выполняться не должна ни при каких условиях.
                    }
                    // raimv1: только прямая запись. raimv2: юзеркоманда +
                    // подстраховка прямой записью (команда — главный канал,
                    // viewAngles — если игра уже применила кадр).
                    else if (raimMode == 1 || !viaCmd) {
                        mem::Write<float>(viewAnglesPtr, angles.x);
                        mem::Write<float>(viewAnglesPtr + 4, angles.y);
                    }

                    // Triggerbot обычного аимбота: стреляем только когда движок
                    // сам подтвердил цель под прицелом (m_iIDEntIndex).
                    if (g_features.reverseAimTrigger.load()) {
                        const int idIndex = mem::Read<int>(localPlayer + off.m_iIDEntIndex);
                        if (idIndex > 0) {
                            const uintptr_t crosshairPawn = ent::GetEntityByIndex(entityList, static_cast<uint32_t>(idIndex));
                            if (crosshairPawn && crosshairPawn != localPlayer &&
                                ent::IsPawnAlive(crosshairPawn)) {
                                INPUT shot{};
                                shot.type = INPUT_MOUSE;
                                shot.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                                if (SendInput(1, &shot, sizeof(INPUT)) == 1) {
                                    shot.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                                    SendInput(1, &shot, sizeof(INPUT));
                                }
                            }
                        }
                    }

                    static uint32_t lastLog = 0;
                    const uint32_t now = GetTickCount();
                    if (now - lastLog > 5000) {
                        lastLog = now;
                        if (raimMode >= 2 && viaCmd) {
                            NW_LOG(L"raimv%d: цель тиммейт (%.0f %.0f %.0f) hp=%d, eye (%.0f %.0f %.0f), углы (%.1f, %.1f) — канал user cmd",
                                   raimMode, targetOrigin.x, targetOrigin.y, targetOrigin.z,
                                   targetHealth, eye.x, eye.y, eye.z, angles.x, angles.y);
                        } else if (raimMode >= 2) {
                            NW_LOG(L"raimv%d: цель тиммейт (%.0f %.0f %.0f) hp=%d, углы (%.1f, %.1f) — канала user cmd нет, probe best %.1f°",
                                   raimMode, targetOrigin.x, targetOrigin.y, targetOrigin.z,
                                   targetHealth, angles.x, angles.y, g_lastProbeScore);
                        } else {
                            NW_LOG(L"raimv%d: цель тиммейт (%.0f %.0f %.0f) hp=%d, eye (%.0f %.0f %.0f), углы (%.1f, %.1f)",
                                   raimMode, targetOrigin.x, targetOrigin.y, targetOrigin.z,
                                   targetHealth, eye.x, eye.y, eye.z, angles.x, angles.y);
                        }
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
                    const ent::Vector3 eyeDbg = ent::GetEyePosition(localPlayer);
                    NW_LOG(L"raimv%d: подходящей цели нет (это НЕ обязательно значит, что все мертвы). primary: slots %d empty %d ctrls %d pawn %d unresolved %d badHandle %d local %d; pawnAlive %d ctrlFlagDead %d; enemies %d teammates %d (team total %d targetable %d). fallback pawn->controller: scanned %d linked %d teammates %d. reject hp %d life %d dormant %d badPos %d close %d immune %d; teams t0=%d/%d t1=%d/%d t2=%d/%d t3=%d/%d other=%d/%d (pawn/pawnAlive); local hp=%d life=%d team=%d eye=%.0f %.0f %.0f",
                           raimMode, stats.slotsScanned, stats.emptySlots, stats.controllers,
                           stats.pawns, stats.unresolvedPawn, stats.invalidHandle, stats.localPawn,
                           stats.alive, stats.pawnAliveButControllerFlagDead,
                           stats.enemies, stats.teammates, totalCount, aliveCount,
                           stats.fallbackScanned, stats.fallbackBackLinked, stats.fallbackTeammates,
                           stats.healthDead, stats.lifeDead, stats.dormant, stats.badPosition,
                           stats.tooClose, stats.immune,
                           stats.teamPawns[0], stats.teamAlive[0],
                           stats.teamPawns[1], stats.teamAlive[1],
                           stats.teamPawns[2], stats.teamAlive[2],
                           stats.teamPawns[3], stats.teamAlive[3],
                           stats.teamPawns[4], stats.teamAlive[4],
                           health, mem::Read<uint8_t>(localPlayer + off.m_lifeState),
                           static_cast<int>(localTeam), eyeDbg.x, eyeDbg.y, eyeDbg.z);

                    // Raw-диагностика именно той цепочки, которая сейчас
                    // ломается на клиенте: controller handle -> entity-list.
                    // Пишется только при отсутствии целей и не меняет память.
                    auto logHandleLookup = [&](int slot, uintptr_t controller, const wchar_t* field, uint32_t handle) {
                        const uint32_t index = handle & ent::kHandleIndexMask;
                        const uintptr_t entryAddress = entityList + off.listEntryOffset +
                            8ull * static_cast<uintptr_t>(index >> 9);
                        const uintptr_t chunk = index ? mem::Read<uintptr_t>(entryAddress) : 0;
                        const uintptr_t elementAddress = chunk
                            ? chunk + off.entryStride * static_cast<uintptr_t>(index & 0x1FF) : 0;
                        const uintptr_t resolved = elementAddress
                            ? mem::Read<uintptr_t>(elementAddress) : 0;
                        NW_LOG(L"raim lookup: slot %d ctrl=0x%llX %s handle=0x%08X idx=%u entry@0x%llX chunk=0x%llX elem@0x%llX pawn=0x%llX",
                               slot, static_cast<unsigned long long>(controller), field, handle, index,
                               static_cast<unsigned long long>(entryAddress),
                               static_cast<unsigned long long>(chunk),
                               static_cast<unsigned long long>(elementAddress),
                               static_cast<unsigned long long>(resolved));
                    };

                    for (int slot = ent::kFirstPlayerSlot; slot <= ent::kMaxPlayerSlots; ++slot) {
                        const uintptr_t controller = ent::GetEntityByIndex(entityList, static_cast<uint32_t>(slot));
                        if (!controller)
                            continue;
                        const uint32_t playerPawn = mem::Read<uint32_t>(controller + off.m_hPlayerPawn);
                        const uint32_t basePawn = mem::Read<uint32_t>(controller + off.m_hPawn);
                        if (ent::IsValidPlayerHandle(playerPawn))
                            logHandleLookup(slot, controller, L"m_hPlayerPawn", playerPawn);
                        if (basePawn != playerPawn && ent::IsValidPlayerHandle(basePawn))
                            logHandleLookup(slot, controller, L"m_hPawn", basePawn);
                    }

                    // test-режим: проверяем канал юзеркоманды даже без цели —
                    // проба раскладки против живых viewAngles.
                    if (raimMode == 3) {
                        const uintptr_t controller =
                            mem::Read<uintptr_t>(clientBase + off.dwLocalPlayerController);
                        const uintptr_t ctx = controller
                            ? mem::Read<uintptr_t>(controller + off.m_CommandContext) : 0;
                        if (!ctx) {
                            NW_LOG(L"test: контроллер 0x%llX ctx 0x%llX — m_CommandContext 0x%llX не читается (IsValidPtr?)",
                                   (unsigned long long)controller, (unsigned long long)ctx, (unsigned long long)off.m_CommandContext);
                        } else if (Raimv2ResolveLayout(ctx, mem::Read<float>(viewAnglesPtr),
                                                       mem::Read<float>(viewAnglesPtr + 4))) {
                            NW_LOG(L"test: раскладка user cmd найдена: ring=0x%X seq=0x%X pb=0x%X base=0x%X msg=0x%X ang=0x%X",
                                   g_cmdLayout.ringBase, g_cmdLayout.seqOff, g_cmdLayout.pbOff,
                                   g_cmdLayout.baseOff, g_cmdLayout.msgOff, g_cmdLayout.angOff);
                        } else {
                            NW_LOG(L"test: проба не сошлась, best %.1f° — раскладки из кандидатов нет",
                                   g_lastProbeScore);
                        }
                    }
                }
            }
        }

        // --- 4b. Антиаимлесс (F2): виден враг — взгляд в пол. ---
        // Тот же метод, что работал до переноса: прямая запись viewAngles.
        if (g_features.antiAimless.load()) {
            bool enemySpotted = false;
            ent::ForEachPlayer(entityList, [&](const ent::PlayerSnapshot& player) {
                if (enemySpotted || player.pawn == localPlayer)
                    return;
                if (player.team != 0 && player.team != localTeam && player.IsTargetable())
                    enemySpotted = true;
            });

            if (enemySpotted) {
                const float spin = g_features.spinSpeed.load();
                const float curYaw = mem::Read<float>(viewAnglesPtr + 4);
                float newYaw = curYaw;
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

        // --- 5. Nonagon Ragebot + Resolver (F6) ---
        if (g_features.ragebot.load()) {
            // Собираем врагов через entity list
            std::vector<nonagon_cs2::CS2Player> storage;
            std::vector<IPlayer*> enemies;
            storage.reserve(64);
            enemies.reserve(64);

            ent::ForEachPlayer(entityList, [&](const ent::PlayerSnapshot& player) {
                if (player.pawn == localPlayer)
                    return;
                if (player.team == 0 || player.team == localTeam)
                    return;
                if (!player.IsTargetable())
                    return;
                storage.emplace_back(player, localTeam, entityList);
            });

            for (auto &p : storage)
                enemies.push_back(&p);

            if (!enemies.empty()) {
                nonagon_cs2::CS2LocalPlayer local(localController, localPlayer, clientBase,
                                                  localTeam, entityList);
                auto &nonagon = nonagon_cs2::GetNonagon();
                WeaponConfig wcfg;
                wcfg.enabled = true;
                wcfg.max_fov = (float)g_features.rageFov.load();
                wcfg.hitchance = g_features.rageHitchance.load();
                wcfg.min_damage = g_features.rageMinDamage.load();
                wcfg.hitboxes = (1 << HITBOX_HEAD) | (1 << HITBOX_CHEST);

                nonagon.cfg.auto_fire = g_features.rageAutoFire.load();
                AimTarget target = nonagon_cs2::SelectTargetWithResolver(
                    &local, enemies, wcfg, nonagon.resolver, nonagon.cfg.head_misses_trigger);

                nonagon_cs2::CS2Player* targetPlayer = nullptr;
                if (target.valid) {
                    for (auto& player : storage) {
                        if (player.GetIndex() == target.playerIndex) {
                            targetPlayer = &player;
                            break;
                        }
                    }
                }

                // Снимок мог устареть между сбором списка и выбором hitbox.
                // Не наводимся и тем более не стреляем без повторной проверки
                // controller -> текущий handle -> тот же живой pawn.
                if (target.valid && targetPlayer && targetPlayer->IsTargetable()) {
                    const Vec3 eye = local.GetEyePos();
                    const Vec3 aimPos = target.aimPos;
                    const Vec3 delta = {aimPos.x - eye.x, aimPos.y - eye.y, aimPos.z - eye.z};
                    const float horizontal = std::sqrtf(delta.x*delta.x + delta.y*delta.y);
                    const float dist = std::sqrtf(horizontal*horizontal + delta.z*delta.z);
                    if (dist > 0.1f) {
                        // Source 2 использует отрицательный pitch для взгляда вверх.
                        float pitch = std::atan2f(-delta.z, std::fmaxf(horizontal, 1.0f)) * ent::kRadToDeg;
                        float yaw = std::atan2f(delta.y, delta.x) * ent::kRadToDeg;
                        ent::NormalizeAngles(pitch, yaw);

                        // NoSpread: перед записью углов компенсируем спред,
                        // если найдены внутренние функции клиента.
                        if (g_features.noSpread.load() && g_userCmdPatterns.ReadyForNoSpread()) {
                            const uintptr_t wsvcR = mem::Read<uintptr_t>(localPlayer + off.m_pWeaponServices);
                            const uint32_t whR = wsvcR ? mem::Read<uint32_t>(wsvcR + off.m_hActiveWeapon) : 0;
                            const uintptr_t wepR = whR ? ent::GetEntityByHandle(entityList, whR) : 0;
                            const uintptr_t vdataR = wepR ? mem::Read<uintptr_t>(wepR + off.m_pWeaponVData) : 0;
                            const int defR = vdataR ? mem::Read<int16_t>(vdataR + 0x1BA) : 0;
                            const uint32_t tickR = localController
                                ? static_cast<uint32_t>(mem::Read<int>(localController + off.m_nTickBase)) : 0;
                            if (defR > 0 && tickR > 0) {
                                const auto nsR = nospread::Solve(
                                    localPlayer, static_cast<int16_t>(defR), tickR,
                                    pitch, yaw, 0.01f, 0.01f, g_userCmdPatterns, 256);
                                if (nsR.ok) {
                                    pitch = nsR.pitch;
                                    yaw = nsR.yaw;
                                }
                            }
                        }

                        bool viaCmd = false;
                        if (g_features.resolver.load())
                            viaCmd = Raimv2WriteToCmd(clientBase, pitch, yaw);

                        bool aimApplied = viaCmd;
                        if (!viaCmd) {
                            const Vector2 angles{pitch, yaw};
                            aimApplied = mem::Write<Vector2>(viewAnglesPtr, angles);
                        }

                        // Настоящий trigger-гейт: ждём, пока игра подтвердит pawn
                        // под прицелом, либо пока угловая ошибка после предыдущего
                        // прохода не станет практически нулевой. Поэтому первый
                        // кадр захвата цели только наводит, следующий уже стреляет.
                        // Стреляем только когда сам движок подтвердил pawn под
                        // crosshair. Старый FOV fallback мог нажать fire по
                        // цели за стеной после одного лишь поворота камеры.
                        const bool triggerConfirmed =
                            ent::IsCrosshairOnPawn(localPlayer, entityList, targetPlayer->GetPawn());

                        static uint32_t lastRageLog = 0;
                        const uint32_t now = GetTickCount();
                        if (now - lastRageLog > 2000) {
                            lastRageLog = now;
                            const auto &state = nonagon.resolver.GetState(target.playerIndex);
                            NW_LOG(L"nonagon rage: цель slot %d pawn 0x%llX hb %d fov %.1f dmg %.0f resolvedYaw %.1f method %d misses %d autofire %s trigger %s",
                                   target.playerIndex + 1,
                                   static_cast<unsigned long long>(targetPlayer->GetPawn()),
                                   static_cast<int>(target.hitbox), target.fov, target.damage,
                                   state.resolvedYaw, static_cast<int>(state.method), state.missedShots,
                                   autoFireEnabled ? L"on" : L"off",
                                   triggerConfirmed ? L"ready" : L"aiming");
                        }

                        // Автоогонь/триггер: угол уже применён, оружие прошло
                        // cooldown-проверку, а цель проверяется ещё раз прямо
                        // перед mouse-down. DOWN и UP разносятся по времени.
                        if (autoFireEnabled && nonagon.cfg.auto_fire && aimApplied &&
                            triggerConfirmed && targetPlayer->IsTargetable() && local.CanFire()) {
                            rageAutoFire.Fire();
                        }
                    }
                } else {
                    static uint32_t lastNoTarget = 0;
                    const uint32_t now = GetTickCount();
                    if (now - lastNoTarget > 5000) {
                        lastNoTarget = now;
                        NW_LOG(L"nonagon rage: целей нет (валидных controller/pawn врагов %d), fov %d hc %d dmg %d",
                               static_cast<int>(enemies.size()), g_features.rageFov.load(),
                               g_features.rageHitchance.load(), g_features.rageMinDamage.load());
                    }
                }
            }
        }

    }
}
