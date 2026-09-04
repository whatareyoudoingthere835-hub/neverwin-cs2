#pragma once
// ============================================================================
// Extrapolation (lagcomp) — port velocity-cs2 (123abc.zip),
// core/features/combat/impl/extrapolation.cpp.
//
// Проблема «аим чуть позади головы»: сервер хитит по СВОЕЙ позиции цели
// (lag comp), а мы видим рендер-позицию с задержкой на (server_tick -
// world_tick) тиков. Симулируем движение hull'ом до серверного тика:
// гравитация (sv_gravity), отскок от стен (trace_hull, 2 итерации slide),
// ground-check (луч 2 юнита вниз, normal.z > 0.7).
//
// Серверный тик: NetworkClientService_001 -> vtable[23]() -> tick_state,
// +892 = server tick (int). Дельта = server_tick - world_tick pawn.
// Лимиты (как в velocity): <= 16 тиков, скорость > 0.1, смена направления
// между двумя последними сэмплами < 35° (иначе экстраполяция мусорит).
//
// История сэмплов: small static map pawn -> 2 последних {origin, simTime,
// worldTick, velocity}. Обновляется из аим-цикла (Update + Extrapolate).
// ============================================================================
#include "pch.h"
#include "memory.hpp"
#include "offsets.hpp"
#include "schema.hpp"
#include "velocity.hpp"
#include "tracing.hpp"
#include "entities.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <array>

namespace ext {

    constexpr float kTickInterval = 1.0f / 64.0f;
    constexpr int kMaxExtrapolateTicks = 16;

    struct Sample {
        bool valid = false;
        float origin[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float simTime = 0.0f;
        int worldTick = 0;
    };

    // --- NetworkClientService: серверный тик -------------------------------
    inline uintptr_t NetworkClient() {
        static uintptr_t cached = 0;
        if (cached) return cached;
        static const wchar_t* kModules[] = { L"engine2.dll", L"server.dll", L"client.dll" };
        for (const wchar_t* m : kModules) {
            cached = schema::GetModuleInterface(m, "NetworkClientService_001");
            if (cached)
                break;
        }
        return cached;
    }

    [[nodiscard]] inline int ServerTick() {
        const uintptr_t net = NetworkClient();
        if (!net)
            return 0;
        // vtable[23]() -> tick_state; +892 = server tick (velocity).
        const uintptr_t tickState = schema_call_vfunc<uintptr_t>(net, 23);
        if (!tickState || !mem::IsValidPtr(reinterpret_cast<const void*>(tickState), 896 + 4))
            return 0;
        return mem::Read<int>(tickState + 892);
    }

    // --- История сэмплов (2 последних на pawn) ----------------------------
    struct PawnHistory {
        Sample last{};
        Sample prev{};
    };

    inline std::map<uintptr_t, PawnHistory>& Histories() {
        static std::map<uintptr_t, PawnHistory> h;
        return h;
    }

    inline void FeedSample(uintptr_t pawn, const ent::Vector3& origin,
                           const ent::Vector3& velocity, int worldTick, float simTime) {
        if (!pawn)
            return;
        auto& h = Histories();
        auto& ph = h[pawn];
        ph.prev = ph.last;
        ph.last.valid = true;
        std::memcpy(ph.last.origin, &origin, 12);
        std::memcpy(ph.last.velocity, &velocity, 12);
        ph.last.worldTick = worldTick;
        ph.last.simTime = simTime;
        // Не даём карте расти: чужие pawn'ы умирают.
        if (h.size() > 32)
            h.erase(h.begin());
    }

    // --- Симуляция одного тика (predict_movement velocity) ----------------
    struct SimData {
        float origin[3]{};
        float velocity[3]{};
        float obbMins[3] = {-16.0f, -16.0f, 0.0f};
        float obbMaxs[3] = {16.0f, 16.0f, 72.0f};
        uint32_t flags = 0;   // bit0 = FL_ONGROUND
    };

    inline void PredictOneTick(SimData& data, uintptr_t skipPawn) {
        static const float kGravity = [] {
            const float g = velo::CvarFloat("sv_gravity", 800.0f);
            return g > 10.0f && g < 10000.0f ? g : 800.0f;
        }();

        if (data.flags & 1u) {
            data.velocity[2] = 0.0f;
        } else {
            data.velocity[2] -= kGravity * kTickInterval;
        }

        float start[3] = { data.origin[0], data.origin[1], data.origin[2] };
        float end[3] = {
            data.origin[0] + data.velocity[0] * kTickInterval,
            data.origin[1] + data.velocity[1] * kTickInterval,
            data.origin[2] + data.velocity[2] * kTickInterval,
        };

        auto tr = trace::TraceHull(start, end, data.obbMins, data.obbMaxs,
                                   skipPawn, trace::kDefaultMask, trace::kDefaultLayer);
        const bool haveTrace = velo::Globals().traceRay != 0;

        if (haveTrace && tr.fraction != 1.0f) {
            // Slide от стены: два прохода (velocity -= normal*dot(vel,normal)).
            for (int i = 0; i < 2; ++i) {
                const float dot = data.velocity[0] * tr.normal[0] +
                                  data.velocity[1] * tr.normal[1] +
                                  data.velocity[2] * tr.normal[2];
                data.velocity[0] -= tr.normal[0] * dot;
                data.velocity[1] -= tr.normal[1] * dot;
                data.velocity[2] -= tr.normal[2] * dot;

                const float remaining = 1.0f - tr.fraction;
                float clipEnd[3] = {
                    tr.endPos[0] + data.velocity[0] * (kTickInterval * remaining),
                    tr.endPos[1] + data.velocity[1] * (kTickInterval * remaining),
                    tr.endPos[2] + data.velocity[2] * (kTickInterval * remaining),
                };
                const auto tr2 = trace::TraceHull(tr.endPos, clipEnd, data.obbMins,
                                                  data.obbMaxs, skipPawn,
                                                  trace::kDefaultMask, trace::kDefaultLayer);
                if (tr2.fraction == 1.0f) {
                    tr = tr2;
                    break;
                }
                tr = tr2;
            }
        }
        data.origin[0] = tr.endPos[0];
        data.origin[1] = tr.endPos[1];
        data.origin[2] = tr.endPos[2];

        // Ground-check: 2 юнита вниз, normal.z > 0.7.
        if (haveTrace) {
            float gStart[3] = { data.origin[0], data.origin[1], data.origin[2] };
            float gEnd[3] = { data.origin[0], data.origin[1], data.origin[2] - 2.0f };
            const auto gtr = trace::TraceHull(gStart, gEnd, data.obbMins, data.obbMaxs,
                                              skipPawn, trace::kDefaultMask,
                                              trace::kDefaultLayer);
            data.flags &= ~1u;
            if (gtr.fraction != 1.0f && gtr.normal[2] > 0.7f)
                data.flags |= 1u;
        }
    }

    // --- OBB цели через m_pCollision (fallback ±16/72) ---------------------
    inline void ReadObb(uintptr_t pawn, float mins[3], float maxs[3]) {
        const float defMins[3] = { -16.0f, -16.0f, 0.0f };
        const float defMaxs[3] = { 16.0f, 16.0f, 72.0f };
        std::memcpy(mins, defMins, 12);
        std::memcpy(maxs, defMaxs, 12);
        if (!pawn)
            return;
        const uintptr_t collision = mem::Read<uintptr_t>(
            pawn + SCHEMA_OFF("C_BaseEntity", "m_pCollision"_hash, 0));
        if (!collision)
            return;
        float m[3] = {0, 0, 0};
        float M[3] = {0, 0, 0};
        if (mem::ReadArray<float>(collision +
            SCHEMA_OFF("CCollisionProperty", "m_vecMins"_hash, 0), m, 3) &&
            m[0] > -256.0f && m[0] < 256.0f)
            std::memcpy(mins, m, 12);
        if (mem::ReadArray<float>(collision +
            SCHEMA_OFF("CCollisionProperty", "m_vecMaxs"_hash, 0), M, 3) &&
            M[0] > -256.0f && M[0] < 512.0f && M[2] > 0.0f)
            std::memcpy(maxs, M, 12);
    }

    // --- Главное: экстраполировать origin до серверного тика ---------------
    // latest — свежий сэмпл (origin = feet pawn, velocity, worldTick).
    // Возвращает true и пишет outOrigin, если экстраполяция применима.
    [[nodiscard]] inline bool Extrapolate(uintptr_t pawn, const Sample& latest,
                                          float outOrigin[3]) {
        if (!pawn || !latest.valid)
            return false;

        const int serverTick = ServerTick();
        if (serverTick <= 0)
            return false;
        const int deltaTicks = serverTick - latest.worldTick;
        if (deltaTicks <= 0 || deltaTicks > kMaxExtrapolateTicks)
            return false;

        const float vx = latest.velocity[0];
        const float vy = latest.velocity[1];
        const float speed = std::sqrtf(vx * vx + vy * vy);
        if (speed < 0.1f)
            return false;

        // Смена направления по двум последним сэмплам: >35° — не экстраполируем
        // (velocity-гэп: резкий поворот/телепорт/спавн).
        float direction = std::atan2f(vy, vx) * (180.0f / 3.14159265f);
        auto& ph = Histories()[pawn];
        const Sample& prev = ph.prev;
        if (prev.valid && ph.last.valid &&
            ph.last.simTime > prev.simTime + 0.0001f) {
            const float dt = ph.last.simTime - prev.simTime;
            const float dx = latest.origin[0] - prev.origin[0];
            const float dy = latest.origin[1] - prev.origin[1];
            if (dx != 0.0f || dy != 0.0f) {
                const float prevDir = std::atan2f(dy, dx) * (180.0f / 3.14159265f);
                float diff = direction - prevDir;
                while (diff > 180.0f) diff -= 360.0f;
                while (diff < -180.0f) diff += 360.0f;
                if (std::fabsf(diff) > 35.0f)
                    return false;
            }
        }

        SimData data{};
        std::memcpy(data.origin, latest.origin, 12);
        std::memcpy(data.velocity, latest.velocity, 12);
        const uint32_t flags = mem::Read<uint32_t>(
            pawn + SCHEMA_OFF("C_BaseEntity", "m_fFlags"_hash, offsets::g.m_fFlags));
        data.flags = flags;
        ReadObb(pawn, data.obbMins, data.obbMaxs);

        for (int i = 0; i < deltaTicks; ++i)
            PredictOneTick(data, pawn);

        const float ddx = data.origin[0] - latest.origin[0];
        const float ddy = data.origin[1] - latest.origin[1];
        const float ddz = data.origin[2] - latest.origin[2];
        if (ddx * ddx + ddy * ddy + ddz * ddz < 0.01f)
            return false;

        std::memcpy(outOrigin, data.origin, 12);
        return true;
    }
}
