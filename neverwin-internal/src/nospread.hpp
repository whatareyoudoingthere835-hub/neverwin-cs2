#pragma once
#include "usercmd_probe.hpp"
#include "offsets.hpp"

// NoSpread по образцу UGame-исходника: подбираем такие view-углы, при которых
// серверный спред-вектор уводит пулю точно в цель. Двигаем pitch на величину
// спред-угла и докручиваем yaw на atan2(spreadX, spreadY).
namespace nospread {
    inline void ent2_norm(float& pitch, float& yaw) {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
    }

    struct Result {
        bool ok = false;
        float pitch = 0.0f; // компенсированные углы
        float yaw = 0.0f;
        float spreadX = 0.0f, spreadY = 0.0f;
    };

    // seed = ComputeRandomSeed(pawn, &viewAngles, tick) — внутренняя функция
    // клиента. Если pattern не найден, NoSpread отключается.
    inline uint32_t ComputeSeed(uintptr_t pawn, float pitch, float yaw,
                                uint32_t tick, uintptr_t fn) {
        if (!fn || !pawn)
            return 0;
        using Fn = uint32_t(__fastcall*)(void*, const float*, uint32_t);
        const float angles[3] = { pitch, yaw, 0.0f };
        return reinterpret_cast<Fn>(fn)(reinterpret_cast<void*>(pawn), angles, tick);
    }

    // spread = CalculateSpread(itemDefIndex, 1, 0, seed+1, inaccuracy, spread,
    //                          recoilIndex, &outX, &outY). Нужен definition
    // index активного оружия.
    inline bool CalcSpread(uintptr_t fn, int16_t itemDefIndex, uint32_t seed,
                           float inaccuracy, float spread, float& outX, float& outY) {
        if (!fn || itemDefIndex <= 0)
            return false;
        using Fn = void(__fastcall*)(int16_t, int, int, uint32_t, float, float, float, float*, float*);
        float x = 0.0f, y = 0.0f;
        reinterpret_cast<Fn>(fn)(itemDefIndex, 1, 0, seed + 1, inaccuracy, spread, 0.0f, &x, &y);
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        outX = x;
        outY = y;
        return true;
    }

    inline float Deg(float rad) { return rad * 57.29577951308232f; }

    // Полный подбор: до maxIterations сдвигов pitch ищем seed, чей спред
    // компенсируется данным углом. Возвращает компенсированные углы.
    inline Result Solve(uintptr_t pawn, int16_t itemDefIndex, uint32_t tick,
                        float aimPitch, float aimYaw, float inaccuracy, float spread,
                        const usercmd_probe::Patterns& p, int maxIterations = 768) {
        Result r;
        if (!p.ReadyForNoSpread() || !pawn || itemDefIndex <= 0)
            return r;

        for (int i = 0; i < maxIterations; ++i) {
            const float testPitch = static_cast<float>(i) * 0.125f;
            const uint32_t seed = ComputeSeed(pawn, testPitch, aimYaw, tick, p.computeRandomSeed);
            if (seed == 0)
                continue;

            float sx = 0.0f, sy = 0.0f;
            if (!CalcSpread(p.calculateSpread, itemDefIndex, seed, inaccuracy, spread, sx, sy))
                continue;

            // Смещаем прицел на величину спреда: pitch вверх на угол вектора,
            // yaw на atan2 компонент. (Источник: NoSpread::NoSpread, UGame.)
            const float mag = std::sqrtf(sx * sx + sy * sy);
            float pitch = testPitch + Deg(std::atanf(mag));
            float yaw = aimYaw - Deg(std::atan2f(sx, sy));
            ent2_norm(pitch, yaw);

            // Верификация: seed компенсированных углов должен совпасть.
            if (ComputeSeed(pawn, pitch, yaw, tick, p.computeRandomSeed) == seed) {
                float vx = 0.0f, vy = 0.0f;
                if (CalcSpread(p.calculateSpread, itemDefIndex, seed, inaccuracy, spread, vx, vy) &&
                    std::fabsf(vx - sx) + std::fabsf(vy - sy) < 0.0005f) {
                    r.ok = true;
                    r.pitch = pitch;
                    r.yaw = yaw;
                    r.spreadX = sx;
                    r.spreadY = sy;
                    return r;
                }
            }
        }
        return r;
    }

} // namespace nospread
