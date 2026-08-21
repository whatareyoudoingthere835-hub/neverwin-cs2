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

    // Полный обход энтити-листа: ВСЕ 32 блока по 512 слотов.
    // Сканирование только блока 0 (хэндлы 1..512) теряло игроков с большими
    // хэндлами — «5 тиммейтов, видно 1» из лога. Пустые блоки пропускаются,
    // так что обход стоит столько же, сколько сущностей в игре.
    template <typename Fn>
    inline void ForEachPawn(uintptr_t entityList, Fn&& fn) {
        for (uint32_t block = 0; block < 32; ++block) {
            const uintptr_t listEntry =
                mem::Read<uintptr_t>(entityList + offsets::g.listEntryOffset + 8ull * block);
            if (!listEntry)
                continue;
            for (uint32_t idx = 0; idx < 512; ++idx) {
                const uintptr_t pawn =
                    mem::Read<uintptr_t>(listEntry + offsets::g.entryStride * idx);
                if (pawn)
                    fn(pawn);
            }
        }
    }

    // Глаза павна: abs origin из сцена-ноды + view offset.
    // m_vecViewOffset (C_BaseModelEntity, 0xE78) — тип CNetworkViewOffsetVector,
    // сетевой: при несовпадении билда/оффсета отдаёт не Vector, а мусор.
    // Мусор в z уводил прицел в зенит (pitch -88°), поэтому диапазоны жёсткие:
    // вылет за пределы — фолбэк на 0/0/64 (стандартная высота глаз стоя).
    inline Vector3 GetEyePosition(uintptr_t pawn) {
        Vector3 origin{};
        const uintptr_t sceneNode = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
        if (sceneNode)
            origin = mem::Read<Vector3>(sceneNode + offsets::g.m_vecAbsOrigin);

        Vector3 viewOffset = mem::Read<Vector3>(pawn + offsets::g.m_vecViewOffset);
        if (viewOffset.x < -100.0f || viewOffset.x > 100.0f) viewOffset.x = 0.0f;
        if (viewOffset.y < -100.0f || viewOffset.y > 100.0f) viewOffset.y = 0.0f;
        if (viewOffset.z < -200.0f || viewOffset.z > 300.0f) viewOffset.z = 64.0f;

        return origin + viewOffset;
    }

    // Команда павна. m_iTeamNum — uint8! Читать его как int нельзя: у части
    // павнов следующий байт ненулевой, и int-чтение даёт мусор (например
    // 0x41xx). На мусорном localTeam фильтр «свой» не срабатывает ни на ком —
    // именно так raim не видел тиммейтов при живом F2.
    inline uint8_t GetTeam(uintptr_t pawn) {
        return mem::Read<uint8_t>(pawn + offsets::g.m_iTeamNum);
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
