#pragma once
// ============================================================================
// Tracing — рейкасты через game_trace_manager (port velocity-cs2, 123abc.zip).
//
// Для чего в NEVERWIN:
//  1. Extrapolation (lagcomp): симуляция позиции цели до серверного тика
//     с отскоком от стен (trace_hull) — лечит «аим чуть позади головы».
//  2. Antiaimless: is_visible — крутить (спин) только когда враг реально
//     видит нас (иначе spin зря ломает свой aim и раскрывает).
//  3. Bhop: trace_player_bbox (movement_services+1592) — точная fraction
//     приземления для subtick-пары.
//
// Структуры (ray/result/filter) — раскладка velocity; вызовы прямые
// (внутренний чит). Все паттерны из velo::Table — если не найдены,
// функции тихо возвращают «нет контакта».
// ============================================================================
#include "pch.h"
#include "memory.hpp"
#include "velocity.hpp"
#include "offsets.hpp"

#include <cmath>
#include <cstring>

namespace trace {

    constexpr uintptr_t kDefaultMask = 0x1C3003;
    constexpr uint8_t kDefaultLayer = 4;
    constexpr int kFilterTypeDefault = 7;

    struct Filter {
        uintptr_t vtable;
        uintptr_t mask;
        int64_t v1[2];
        int skipHandles[4];
        int16_t collisions[2];
        int16_t v2;
        uint8_t layer;
        uint8_t flags;
        uint8_t v5;
        uint8_t v6;
        uint8_t pad0[6];
        char v7;
    };

    struct Ray {
        float mins[3] = {0, 0, 0};
        float maxs[3] = {0, 0, 0};
        uint8_t pad0[0x10] = {0};
        uint8_t type = 0;      // 0 = ray, 1 = sphere, 2 = hull
        uint8_t pad1[7] = {0};
    };

    struct Result {
        void* surface;
        uintptr_t hitEntity;
        void* hitboxData;
        uint8_t pad0[0x38];
        uint32_t contents;
        uint8_t pad1[0x24];
        float startPos[3];
        float endPos[3];
        float normal[3];
        float position[3];
        uint8_t pad2[4];
        float fraction;
        uint8_t pad3[6];
        bool allSolid;
        uint8_t pad4[0x4D];
    };

    struct PlayerMovementFilter { uint8_t data[0x48] = {0}; };
    struct BBox {
        float mins[3] = {0, 0, 0};
        float maxs[3] = {0, 0, 0};
        BBox() = default;
        // C++17: aggregate-инициализация float[3] члена из float[3] lvalue
        // запрещена — конструктор с memcpy.
        BBox(const float mn[3], const float mx[3]) {
            std::memcpy(mins, mn, 12);
            std::memcpy(maxs, mx, 12);
        }
    };

    inline bool MakeFilter(Filter& out, uintptr_t skipEntity, uintptr_t mask,
                           uint8_t layer, int type = kFilterTypeDefault) {
        const uintptr_t fn = velo::Globals().traceFilterInit;
        if (!fn || !mem::IsValidPtr(reinterpret_cast<const void*>(fn), 16))
            return false;
        std::memset(&out, 0, sizeof(out));
        using Fn = void(__fastcall*)(Filter*, uintptr_t, uintptr_t, uint8_t, int);
        reinterpret_cast<Fn>(fn)(&out, skipEntity, mask, layer, type);
        return true;
    }

    inline bool MakePlayerMovementFilter(PlayerMovementFilter& out, uintptr_t entity,
                                         uintptr_t mask, int collisionGroup) {
        const uintptr_t fn = velo::Globals().traceFilterSetCollision;
        if (!fn || !mem::IsValidPtr(reinterpret_cast<const void*>(fn), 16))
            return false;
        std::memset(&out, 0, sizeof(out));
        using Fn = void(__fastcall*)(PlayerMovementFilter*, uintptr_t, uintptr_t, int);
        reinterpret_cast<Fn>(fn)(&out, entity, mask, collisionGroup);
        return true;
    }

    inline bool TraceRay(Ray& ray, const float start[3], const float end[3],
                         const Filter& filter, Result& out) {
        const uintptr_t fn = velo::Globals().traceRay;
        const uintptr_t mgr = velo::Globals().gameTraceManager;
        if (!fn || !mgr || !mem::IsValidPtr(reinterpret_cast<const void*>(fn), 16))
            return false;
        std::memset(&out, 0, sizeof(out));
        using Fn = bool(__fastcall*)(uintptr_t, Ray*, const float*, const float*,
                                     const Filter*, Result*);
        reinterpret_cast<Fn>(fn)(mgr, &ray, start, end, &filter, &out);
        return true;
    }

    // Простой луч (type 0).
    inline Result Trace(const float start[3], const float end[3],
                        uintptr_t skipEntity = 0, uintptr_t mask = kDefaultMask,
                        uint8_t layer = kDefaultLayer) {
        Result r{};
        Filter f{};
        Ray ray{};
        if (!MakeFilter(f, skipEntity, mask, layer) || !TraceRay(ray, start, end, f, r))
            return {};
        return r;
    }

    // Hull (type 2) с OBB mins/maxs.
    inline Result TraceHull(const float start[3], const float end[3],
                            const float mins[3], const float maxs[3],
                            uintptr_t skipEntity = 0, uintptr_t mask = kDefaultMask,
                            uint8_t layer = kDefaultLayer) {
        Result r{};
        Filter f{};
        Ray ray{};
        std::memcpy(ray.mins, mins, 12);
        std::memcpy(ray.maxs, maxs, 12);
        ray.type = 2;
        if (!MakeFilter(f, skipEntity, mask, layer) || !TraceRay(ray, start, end, f, r))
            return {};
        return r;
    }

    // Бокс локального игрока через movement_services (bhop-приземление).
    inline Result TracePlayerBBox(const float start[3], const float end[3],
                                  const BBox& bbox, const PlayerMovementFilter& filter,
                                  uintptr_t movementServices) {
        Result r{};
        if (!movementServices)
            return {};
        using Fn = void(__fastcall*)(Result*, const float*, const float*,
                                     const BBox*, const PlayerMovementFilter*);
        reinterpret_cast<Fn>(movementServices + 1592)(&r, start, end, &bbox, &filter);
        return r;
    }

    // Видимость с учётом пробок живыми игроками (до 3 penetration'ов),
    // ровно как в velocity::tracing::is_visible.
    inline bool IsVisible(const float start[3], const float end[3],
                          uintptr_t targetEntity, uintptr_t skipEntity = 0,
                          uintptr_t mask = kDefaultMask) {
        const auto& g = velo::Globals();
        if (!g.gameTraceManager || !g.traceRay || !g.traceFilterInit)
            return false;

        float curStart[3] = { start[0], start[1], start[2] };
        uintptr_t skip = skipEntity;
        constexpr int kMaxPenetrations = 3;

        for (int i = 0; i < kMaxPenetrations; ++i) {
            Filter f{};
            Ray ray{};
            Result r{};
            if (!MakeFilter(f, skip, mask, kDefaultLayer) || !TraceRay(ray, curStart, end, f, r))
                return false;

            if (r.hitEntity == targetEntity || r.fraction > 0.97f)
                return true;
            if (!r.hitEntity)
                break;

            const int hitHealth = mem::Read<int>(
                r.hitEntity + SCHEMA_OFF("C_BaseEntity", "m_iHealth"_hash,
                                         offsets::g.m_iHealth));
            if (hitHealth > 0 && hitHealth <= 100) {
                skip = r.hitEntity;
                const float dx = end[0] - curStart[0];
                const float dy = end[1] - curStart[1];
                const float dz = end[2] - curStart[2];
                const float len = std::sqrtf(dx * dx + dy * dy + dz * dz);
                if (len < 0.001f)
                    break;
                curStart[0] = r.endPos[0] + dx / len;
                curStart[1] = r.endPos[1] + dy / len;
                curStart[2] = r.endPos[2] + dz / len;
                continue;
            }
            break;
        }
        return false;
    }
}
