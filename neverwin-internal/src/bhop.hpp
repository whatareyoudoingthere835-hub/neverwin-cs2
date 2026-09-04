#pragma once
// ============================================================================
// Bhop — предсказание приземления (port velocity-cs2, 123abc.zip,
// core/features/movement/impl/bunnyhop.cpp).
//
// Старый velobhop «постфактум» замечал приземление по смене fFlags и ставил
// subtick-пару (release tickStart / press tickEnd) на ТИК приземления.
// Velocity делает точнее: пока мы в воздухе и зажат прыжок, считает fraction
// `f`, на котором pawn приземлится ВНУТРИ текущего тика (trace_player_bbox
// через movement_services+1592, mask из pawn+0xd48, FL_DUCKING |= 0x20),
// и пишет пару release(f-1/64) + press(f) — ре-пресс ровно в момент
// приземления, прыжок не теряется.
//
// Выход: fraction 0..1 (где в текущем тике приземление) или -1 (нет/неверно).
// ============================================================================
#include "pch.h"
#include "memory.hpp"
#include "offsets.hpp"
#include "schema.hpp"
#include "velocity.hpp"
#include "tracing.hpp"
#include "entities.hpp"

#include <cmath>
#include <algorithm>

namespace bhop {

    constexpr float kTickInterval = 1.0f / 64.0f;

    [[nodiscard]] inline float PredictLandingFraction(uintptr_t pawn,
        uintptr_t movementServices, const ent::Vector3& networkedOrigin,
        const ent::Vector3& networkedVelocity, uint32_t flags, bool holdingDuck) {
        if (!pawn || !movementServices)
            return -1.0f;
        if (networkedVelocity.z > 0.0f)
            return -1.0f;   // поднимаемся — приземления в этом тике не будет

        // m_flDuckAmount (CCSPlayer_MovementServices) — через schema.
        const float duckAmount = mem::Read<float>(
            movementServices + SCHEMA_OFF("CCSPlayer_MovementServices",
                                          "m_flDuckAmount"_hash, 0));
        const float collisionBase =
            SCHEMA_OFF("C_BaseModelEntity", "m_Collision"_hash, 0);
        const uintptr_t collision = mem::Read<uintptr_t>(pawn + collisionBase);
        if (!collision)
            return -1.0f;
        float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
        if (!mem::ReadArray<float>(collision +
            SCHEMA_OFF("CCollisionProperty", "m_vecMins"_hash, 0), mins, 3) ||
            !mem::ReadArray<float>(collision +
            SCHEMA_OFF("CCollisionProperty", "m_vecMaxs"_hash, 0), maxs, 3))
            return -1.0f;
        // Защита от мусора (schema не поднялась → чтение по оффсету 0 = vtable):
        // человеческий hull ~ (±16, ±16, 0..72).
        const bool minsOk = mins[0] > -100.0f && mins[0] < 100.0f &&
                            mins[1] > -100.0f && mins[1] < 100.0f &&
                            mins[2] > -100.0f && mins[2] < 100.0f;
        const bool maxsOk = maxs[0] > -100.0f && maxs[0] < 100.0f &&
                            maxs[1] > -100.0f && maxs[1] < 100.0f &&
                            maxs[2] > 10.0f && maxs[2] < 150.0f;
        if (!minsOk || !maxsOk) {
            mins[0] = -16.0f; mins[1] = -16.0f; mins[2] = 0.0f;
            maxs[0] = 16.0f;  maxs[1] = 16.0f;  maxs[2] = 72.0f;
        }

        float traceOrigin[3] = { networkedOrigin.x, networkedOrigin.y, networkedOrigin.z };
        if (holdingDuck && duckAmount > 0.0f) {
            const float standingHeight = 72.0f;
            const float duckHullDiff = standingHeight - maxs[2];
            traceOrigin[2] -= duckHullDiff * 0.5f;
            maxs[2] = standingHeight;
        }

        // Mask из runtime-состояния pawn (0xd48 в velocity-билдах);
        // при ducking добавляем SOLID_DUCKABLE-подобный бит 0x20.
        uintptr_t traceMask = mem::Read<uintptr_t>(pawn + 0xD48);
        const uint32_t fflags = mem::Read<uint32_t>(pawn +
            SCHEMA_OFF("C_BaseEntity", "m_fFlags"_hash, offsets::g.m_fFlags));
        if (traceMask == 0)
            traceMask = trace::kDefaultMask;
        if (fflags & 0x10u /* FL_DUCKING */)
            traceMask |= 0x20;

        trace::PlayerMovementFilter filter{};
        if (!trace::MakePlayerMovementFilter(filter, pawn, traceMask, 11))
            return -1.0f;

        const float svGravity = velo::CvarFloat("sv_gravity", 800.0f);
        const float svStandable = velo::CvarFloat("sv_standable_normal", 0.7f);
        const float gravityScale = mem::Read<float>(pawn +
            SCHEMA_OFF("C_BaseEntity", "m_flGravityScale"_hash, 1.0f));
        (void)flags;

        const float velZ = networkedVelocity.z -
            (gravityScale * svGravity * kTickInterval) * 0.5f;
        const float start[3] = { traceOrigin[0], traceOrigin[1], traceOrigin[2] };
        float end[3] = {
            traceOrigin[0] + networkedVelocity.x * kTickInterval,
            traceOrigin[1] + networkedVelocity.y * kTickInterval,
            traceOrigin[2] + velZ * kTickInterval,
        };
        end[2] -= 2.0f;

        const trace::BBox bbox{ mins, maxs };
        const auto result = trace::TracePlayerBBox(start, end,
            bbox, filter, movementServices);
        if (result.fraction <= 0.0f || result.fraction >= 1.0f ||
            result.normal[2] < svStandable)
            return -1.0f;

        return std::clamp(std::roundf(result.fraction * 64.0f) / 64.0f,
                          1.0f / 64.0f, 63.0f / 64.0f);
    }
}
