#pragma once
// ============================================================================
// Общие хелперы по сущностям: энтити-лист, глаза, углы.
// Используются и циклом фич (gamesense), и хуком CreateMove (F1/F2).
// ============================================================================
#include "memory.hpp"
#include "offsets.hpp"

#include <cmath>

namespace ent {

    struct Vector2 { float x = 0.0f; float y = 0.0f; };
    struct Vector3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };

    inline Vector3 operator+(const Vector3& a, const Vector3& b) {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    constexpr float kRadToDeg = 57.29577951308232f;

    // --- Энтити-лист Source 2: список списков. ---
    // listEntry = entityList + 0x10 + 8 * (index >> 9)
    // element   = listEntry  + 0x78 * (index & 0x1FF)
    inline uintptr_t GetEntityByHandle(uintptr_t entityList, uint32_t handle) {
        const uint32_t index = handle & 0x7FFF;
        const uintptr_t listEntry =
            mem::Read<uintptr_t>(entityList + offsets::g.listEntryOffset + 8ull * (index >> 9));
        if (!listEntry)
            return 0;
        return mem::Read<uintptr_t>(listEntry + offsets::g.entryStride * (index & 0x1FF));
    }

    // Глаза павна: abs origin из сцена-ноды + view offset.
    // m_vecViewOffset — поле C_BaseModelEntity, читается прямо из павна.
    inline Vector3 GetEyePosition(uintptr_t pawn) {
        Vector3 origin{};
        const uintptr_t sceneNode = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
        if (sceneNode)
            origin = mem::Read<Vector3>(sceneNode + offsets::g.m_vecAbsOrigin);

        const Vector3 viewOffset = mem::Read<Vector3>(pawn + offsets::g.m_vecViewOffset);
        return origin + viewOffset;
    }

    // Углы из точки A в точку B. Конвенция Source: pitch вверх отрицательный,
    // yaw из atan2 уже лежит в [-180, 180].
    inline Vector2 CalcAngles(const Vector3& from, const Vector3& to) {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float dz = to.z - from.z;
        // Минимум 1 юнит горизонтали — atan2 не увидит NaN, когда цель
        // стоит ровно на тебе (или на той же XY-точке).
        const float dist2d = std::fmaxf(std::sqrtf(dx * dx + dy * dy), 1.0f);

        Vector2 angles{};
        angles.x = std::atan2f(-dz, dist2d) * kRadToDeg;
        angles.y = std::atan2f(dy, dx) * kRadToDeg;
        return angles;
    }

    inline void NormalizeAngles(float& pitch, float& yaw) {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
    }

    inline bool IsSaneAngles(float pitch, float yaw) {
        return std::isfinite(pitch) && std::isfinite(yaw) &&
               std::fabsf(pitch) <= 90.5f && std::fabsf(yaw) <= 360.5f;
    }

} // namespace ent
