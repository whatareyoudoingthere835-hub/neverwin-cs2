#include "pch.h"
#include "input_hooks.hpp"

#include "entities.hpp"
#include "features.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "offsets.hpp"

#include "minhook.h"

#include <cfloat>

// ============================================================================
// Канал записи углов: CSGOInput::CreateMove (vtable 5), как в quintcs2.
//
// Почему так: CS2 каждый тик переписывает dwViewAngles из юзеркоманды —
// писать напрямую во viewAngles из фонового потока значит гоняться с игрой.
// Правильно: после оригинала CreateMove переписать углы в текущей
// юзеркоманде — игра использует их весь тик.
//
// Цепочка БЕЗ вызовов функций клиента (в v6 был вызов GetCmdManager по
// сигнатуре quint — сигнатура от их билда, на нашем клиенте матчится мимо,
// и вызов уводил в мусор):
//   dwLocalPlayerController -> controller            (из нашего дампа)
//   controller + m_CommandContext -> CCommandContext (схема, из дампа)
//   ctx + 0x59A8 = m_sequence, cmd = ctx + (seq%150)*0x98
//   cmd -> pb (0x10/0x18, рантайм-проба) -> base_cmd (0x28/0x30, проба)
//   base_cmd + 0x40 = m_view_angles (c_msg_q_angle*)
//   msg + 0x18 = QAngle, msg + 0x10 = cached_bits (|= 7 — углы заданы)
//
// Все шаги — mem::Read с проверкой VirtualQuery, вызовов нет вообще.
// Раскладка pb/base решается пробой: углы msg сверяются с живыми
// dwViewAngles, победитель логируется.
// ============================================================================

namespace {

    using CreateMoveFn = void (__fastcall*)(void*, int, bool);

    CreateMoveFn g_origCreateMove = nullptr;
    uintptr_t    g_clientBase     = 0;
    uintptr_t    g_viewAnglesPtr  = 0;
    uintptr_t    g_localPlayerPtr = 0;

    // Раскладка протобуфа юзеркоманды (решается пробой, см. выше).
    struct CmdLayout {
        uint32_t pbOff    = 0;
        uint32_t baseOff  = 0;
        bool     resolved = false;
    } g_layout;

    // --- рантайм-проба раскладки user cmd ---
    bool ResolveLayout(uintptr_t cmd) {
        const float livePitch = mem::Read<float>(g_viewAnglesPtr);
        const float liveYaw   = mem::Read<float>(g_viewAnglesPtr + 4);
        if (!ent::IsSaneAngles(livePitch, liveYaw))
            return false; // клиент ещё не в игре, сравнивать не с чем

        struct Cand { uint32_t pb; uint32_t base; float score; } best{};
        best.score = FLT_MAX;

        const uint32_t pbOffs[]   = { 0x10, 0x18 };
        const uint32_t baseOffs[] = { 0x28, 0x30 };

        for (uint32_t pb : pbOffs) {
            const uintptr_t pbAddr = mem::Read<uintptr_t>(cmd + pb);
            if (!pbAddr)
                continue;
            for (uint32_t base : baseOffs) {
                const uintptr_t baseCmd = mem::Read<uintptr_t>(pbAddr + base);
                if (!baseCmd)
                    continue;
                const uintptr_t msg = mem::Read<uintptr_t>(baseCmd + 0x40);
                if (!msg)
                    continue;
                const ent::Vector3 ang = mem::Read<ent::Vector3>(msg + 0x18);
                if (!ent::IsSaneAngles(ang.x, ang.y))
                    continue;
                float dy = std::fabsf(ang.y - liveYaw);
                if (dy > 180.0f)
                    dy = 360.0f - dy;
                const float score = std::fabsf(ang.x - livePitch) + dy;
                if (score < best.score) {
                    best.pb = pb;
                    best.base = base;
                    best.score = score;
                }
            }
        }

        // Углы cmd и углы рендера в одном тике — в пределах пары градусов.
        if (best.score <= 25.0f) {
            g_layout.pbOff = best.pb;
            g_layout.baseOff = best.base;
            g_layout.resolved = true;
            NW_LOG(L"раскладка user cmd: pb=0x%X base_cmd=0x%X (отклонение %.1f°)",
                   best.pb, best.base, best.score);
            return true;
        }
        return false;
    }

    // --- запись углов в текущий user cmd (только чтения + запись в хип) ---
    bool WriteAnglesViaCmd(float pitch, float yaw) {
        const uintptr_t controller =
            mem::Read<uintptr_t>(g_clientBase + offsets::g.dwLocalPlayerController);
        if (!controller)
            return false;

        const uintptr_t ctx =
            mem::Read<uintptr_t>(controller + offsets::g.m_CommandContext);
        if (!ctx)
            return false;

        const int seq = mem::Read<int>(ctx + 0x59A8); // m_sequence
        const uintptr_t cmd = ctx + static_cast<uintptr_t>(seq % 150) * 0x98;

        if (!g_layout.resolved && !ResolveLayout(cmd))
            return false;

        const uintptr_t pb = mem::Read<uintptr_t>(cmd + g_layout.pbOff);
        if (!pb)
            return false;
        const uintptr_t baseCmd = mem::Read<uintptr_t>(pb + g_layout.baseOff);
        if (!baseCmd)
            return false;
        const uintptr_t msg = mem::Read<uintptr_t>(baseCmd + 0x40); // m_view_angles
        if (!msg)
            return false;

        const ent::Vector3 ang{ pitch, yaw, 0.0f };
        mem::Write<ent::Vector3>(msg + 0x18, ang);                 // m_ang_value
        const uint64_t bits = mem::Read<uint64_t>(msg + 0x10);     // m_cached_bits
        mem::Write<uint64_t>(msg + 0x10, bits | 7u);               // углы заданы
        return true;
    }

    // --- хук CreateMove: после оригинала переписываем углы F1/F2 ---
    void __fastcall HookedCreateMove(void* input, int slot, bool active) {
        if (g_origCreateMove)
            g_origCreateMove(input, slot, active);

        const bool wantAimbot   = g_features.antiAimbot.load();
        const bool wantAimless  = g_features.antiAimless.load();
        if (!wantAimbot && !wantAimless)
            return;

        const uintptr_t local = mem::Read<uintptr_t>(g_localPlayerPtr);
        if (!local || mem::Read<int>(local + offsets::g.m_iHealth) <= 0)
            return;

        const int localTeam = mem::Read<int>(local + offsets::g.m_iTeamNum);
        const uintptr_t entityList = mem::Read<uintptr_t>(g_clientBase + offsets::g.dwEntityList);
        if (!entityList)
            return;

        const ent::Vector3 eye = ent::GetEyePosition(local);
        if (eye.x == 0.0f && eye.y == 0.0f && eye.z == 0.0f)
            return;

        float pitch = 0.0f, yaw = 0.0f;
        bool have = false;

        if (wantAimbot) {
            // F1: ближайший живой тиммейт, цель origin+64 (корпус/голова).
            uintptr_t bestPawn = 0;
            ent::Vector3 bestOrigin{};
            float bestDist2 = FLT_MAX;

            for (uint32_t i = 1; i < 64; ++i) {
                const uintptr_t pawn = ent::GetEntityByHandle(entityList, i);
                if (!pawn || pawn == local)
                    continue;
                if (mem::Read<int>(pawn + offsets::g.m_iHealth) <= 0)
                    continue;
                if (mem::Read<int>(pawn + offsets::g.m_iTeamNum) != localTeam)
                    continue;

                const uintptr_t node = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
                if (!node)
                    continue;
                const ent::Vector3 origin = mem::Read<ent::Vector3>(node + offsets::g.m_vecAbsOrigin);

                const float dx = origin.x - eye.x;
                const float dy = origin.y - eye.y;
                const float dz = origin.z - eye.z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < bestDist2) {
                    bestDist2 = d2;
                    bestPawn = pawn;
                    bestOrigin = origin;
                }
            }

            if (bestPawn) {
                const ent::Vector3 target{ bestOrigin.x, bestOrigin.y, bestOrigin.z + 64.0f };
                const ent::Vector2 ang = ent::CalcAngles(eye, target);
                pitch = ang.x;
                yaw = ang.y;
                have = true;
            }
        } else if (wantAimless) {
            // F2: виден враг — взгляд в пол.
            bool enemy = false;
            for (uint32_t i = 1; i < 64 && !enemy; ++i) {
                const uintptr_t pawn = ent::GetEntityByHandle(entityList, i);
                if (!pawn || pawn == local)
                    continue;
                if (mem::Read<int>(pawn + offsets::g.m_iHealth) <= 0)
                    continue;
                if (mem::Read<int>(pawn + offsets::g.m_iTeamNum) != localTeam)
                    enemy = true;
            }
            if (enemy) {
                const float curYaw = mem::Read<float>(g_viewAnglesPtr + 4);
                pitch = 89.0f;
                yaw = curYaw + 15.0f;
                if (yaw > 180.0f)
                    yaw -= 360.0f;
                if (yaw < -180.0f)
                    yaw += 360.0f;
                have = true;
            }
        }

        if (!have)
            return;

        ent::NormalizeAngles(pitch, yaw);

        if (WriteAnglesViaCmd(pitch, yaw)) {
            static bool logged = false;
            if (!logged) {
                NW_LOG(L"F1/F2: углы пишутся в user cmd (CreateMove).");
                logged = true;
            }
            return;
        }

        // Фолбэк: прямой write во viewAngles (игра может перезаписать —
        // но хуже не будет, чем было).
        mem::Write<float>(g_viewAnglesPtr, pitch);
        mem::Write<float>(g_viewAnglesPtr + 4, yaw);
        static bool loggedFb = false;
        if (!loggedFb) {
            NW_LOG(L"WARNING: user cmd недоступен — F1/F2 пишут во viewAngles напрямую.");
            loggedFb = true;
        }
    }

} // namespace

namespace inhooks {

    bool Init() {
        HMODULE cl = GetModuleHandleW(L"client.dll");
        for (int i = 0; i < 300 && !cl; ++i) {
            Sleep(100);
            cl = GetModuleHandleW(L"client.dll");
        }
        if (!cl) {
            NW_LOG(L"input_hooks: client.dll не найден.");
            return false;
        }
        g_clientBase = reinterpret_cast<uintptr_t>(cl);
        g_viewAnglesPtr  = g_clientBase + offsets::g.dwViewAngles;
        g_localPlayerPtr = g_clientBase + offsets::g.dwLocalPlayerPawn;

        // CSGOInput может быть null в меню — ждём до 60с.
        uintptr_t csgoInput = 0;
        for (int i = 0; i < 600 && !csgoInput; ++i) {
            csgoInput = mem::Read<uintptr_t>(g_clientBase + offsets::g.dwCSGOInput);
            if (!csgoInput)
                Sleep(100);
        }
        if (!csgoInput) {
            NW_LOG(L"input_hooks: CSGOInput не найден (dwCSGOInput=0x%X) — F1/F2 через viewAngles.",
                   static_cast<unsigned>(offsets::g.dwCSGOInput));
            return false;
        }
        NW_LOG(L"CSGOInput: 0x%llX", static_cast<unsigned long long>(csgoInput));

        void** vt = *reinterpret_cast<void***>(csgoInput);
        const LPVOID target = reinterpret_cast<LPVOID>(vt[5]); // CreateMove
        if (!target ||
            MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookedCreateMove),
                          reinterpret_cast<LPVOID*>(&g_origCreateMove)) != MH_OK ||
            MH_EnableHook(target) != MH_OK) {
            NW_LOG(L"input_hooks: CreateMove (vtable 5) не захучен — F1/F2 через viewAngles.");
            return false;
        }

        NW_LOG(L"CreateMove захучен — F1/F2 пишут углы в user cmd (m_CommandContext).");
        return true;
    }
}
