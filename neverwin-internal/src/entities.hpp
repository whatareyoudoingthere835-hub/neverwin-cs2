#pragma once
// ============================================================================
// Общие хелперы по сущностям: энтити-лист, игроки, глаза и углы.
//
// Важно: игроки перечисляются через CCSPlayerController (слоты 1..64), а не
// сканированием всех 32768 сущностей как будто каждая из них является pawn.
// Старый обход принимал оружие, трупы и другие сущности за игроков, если байты
// по смещениям health/team случайно выглядели правдоподобно.
// ============================================================================
#include "memory.hpp"
#include "offsets.hpp"

#include <cmath>
#include <cstdint>

namespace ent {

    struct Vector2 { float x = 0.0f; float y = 0.0f; };
    struct Vector3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };

    inline Vector3 operator+(const Vector3& a, const Vector3& b) {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    constexpr float kRadToDeg = 57.29577951308232f;
    constexpr uint32_t kHandleIndexMask = 0x7FFFu;
    constexpr uint32_t kInvalidHandleIndex = kHandleIndexMask;
    constexpr int kFirstPlayerSlot = 1;
    constexpr int kMaxPlayerSlots = 64;

    // --- Энтити-лист Source 2: список списков. ---
    // listEntry = entityList + 0x10 + 8 * (index >> 9)
    // element   = listEntry  + 0x78 * (index & 0x1FF)
    inline uintptr_t GetEntityByHandle(uintptr_t entityList, uint32_t handle) {
        if (!entityList)
            return 0;

        const uint32_t index = handle & kHandleIndexMask;
        if (index == 0 || index == kInvalidHandleIndex)
            return 0;

        const uintptr_t listEntry =
            mem::Read<uintptr_t>(entityList + offsets::g.listEntryOffset + 8ull * (index >> 9));
        if (!listEntry)
            return 0;
        return mem::Read<uintptr_t>(listEntry + offsets::g.entryStride * (index & 0x1FF));
    }

    inline uintptr_t GetEntityByIndex(uintptr_t entityList, uint32_t index) {
        return GetEntityByHandle(entityList, index);
    }

    // Если обновление меняет внутреннюю позицию таблицы chunks в
    // CGameEntitySystem, жёсткие +0x10/+0x78 начинают возвращать валидные, но
    // совершенно чужие адреса. Ищем layout по единственному надёжно известному
    // объекту — local pawn, полученному через dwLocalPlayerPawn. Совпадение
    // именно указателя pawn исключает угадывание по health/team.
    inline bool DiscoverEntityListLayout(uintptr_t entitySystem, uintptr_t rawGlobalAddress,
                                         uintptr_t knownPawn,
                                         uint32_t knownHandle, uintptr_t& outSystem,
                                         uintptr_t& outListOffset, uintptr_t& outStride) {
        if ((!entitySystem && !rawGlobalAddress) || !knownPawn)
            return false;

        // 14177 может отдавать систему либо по указателю из dwEntityList,
        // либо напрямую по самому адресу глобала — проверяем оба корня.
        const uintptr_t roots[] = {
            entitySystem,
            rawGlobalAddress,
            entitySystem ? mem::Read<uintptr_t>(entitySystem) : 0,
            entitySystem ? mem::Read<uintptr_t>(entitySystem + 8) : 0,
            entitySystem ? mem::Read<uintptr_t>(entitySystem + 0x10) : 0,
        };
        constexpr uintptr_t strides[] = { 0x78, 0x80, 0x70 };
        constexpr int kChunkCount = 64;
        constexpr int kEntriesPerChunk = 512;

        // Нормальный CHandle уже содержит индекс local pawn. Этот быстрый
        // путь проверяет ровно соответствующую ячейку и не делает тяжёлый
        // полный обход таблицы в игровом тике.
        const uint32_t knownIndex = knownHandle & kHandleIndexMask;
        if (knownIndex != 0 && knownIndex != kInvalidHandleIndex) {
            for (uintptr_t root : roots) {
                if (!root || !mem::IsValidPtr(reinterpret_cast<const void*>(root), 0x20))
                    continue;
                for (uintptr_t listOffset = 0; listOffset <= 0x400; listOffset += 8) {
                    const uintptr_t chunk = mem::Read<uintptr_t>(
                        root + listOffset + 8ull * static_cast<uintptr_t>(knownIndex >> 9));
                    if (!chunk)
                        continue;
                    for (uintptr_t stride : strides) {
                        if (mem::Read<uintptr_t>(chunk + stride * static_cast<uintptr_t>(knownIndex & 0x1FF)) == knownPawn) {
                            outSystem = root;
                            outListOffset = listOffset;
                            outStride = stride;
                            return true;
                        }
                    }
                }
            }
            // Быстрый путь не сошёлся (например, offset handle-поля сместился
            // в новом билде) — падаем в ограниченный полный поиск ниже, а не
            // сдаёмся сразу.
        }

        // Полный поиск: chunk валидируется один раз, дальше сырые чтения —
        // это дешёво даже для 512*3 ячеек на chunk.
        for (uintptr_t root : roots) {
            if (!root || !mem::IsValidPtr(reinterpret_cast<const void*>(root), 0x20))
                continue;
            for (uintptr_t listOffset = 0; listOffset <= 0x80; listOffset += 8) {
                for (int chunkIndex = 0; chunkIndex < kChunkCount; ++chunkIndex) {
                    const uintptr_t chunk = mem::Read<uintptr_t>(
                        root + listOffset + 8ull * static_cast<uintptr_t>(chunkIndex));
                    if (!chunk)
                        continue;
                    constexpr size_t kChunkBytes = kEntriesPerChunk * 0x80;
                    if (!mem::IsValidPtr(reinterpret_cast<const void*>(chunk), kChunkBytes))
                        continue;
                    for (uintptr_t stride : strides) {
                        const uintptr_t* cells = reinterpret_cast<const uintptr_t*>(chunk);
                        for (int entry = 0; entry < kEntriesPerChunk; ++entry) {
                            const uintptr_t candidate = cells[entry * (stride / sizeof(uintptr_t))];
                            if (candidate == knownPawn) {
                                outSystem = root;
                                outListOffset = listOffset;
                                outStride = stride;
                                return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    inline bool IsValidPlayerHandle(uint32_t handle) {
        const uint32_t index = handle & kHandleIndexMask;
        return handle != 0 && index != 0 && index != kInvalidHandleIndex;
    }

    inline bool IsFiniteVector(const Vector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    inline bool IsUsablePlayerOrigin(const Vector3& v) {
        if (!IsFiniteVector(v))
            return false;
        // (0,0,0) у неинициализированного/удалённого pawn встречается часто.
        return v.x != 0.0f || v.y != 0.0f || v.z != 0.0f;
    }

    // Один согласованный снимок controller -> handle -> pawn. Живым игрок
    // считается только когда ВСЕ авторитетные признаки согласны:
    // controller.m_bPawnIsAlive, текущий m_hPlayerPawn, health и lifeState.
    struct PlayerSnapshot {
        int slot = 0;
        uintptr_t controller = 0;
        uint32_t pawnHandle = 0;
        uintptr_t pawn = 0;
        uintptr_t sceneNode = 0;
        uint8_t team = 0;
        int health = 0;
        uint8_t lifeState = 0xFF;
        bool controllerAlive = false;
        bool dormant = true;
        bool immune = false;
        Vector3 origin{};
        Vector3 velocity{};

        bool HasController() const { return controller != 0; }
        bool HasPawn() const { return pawn != 0; }

        // Состояние самого pawn. Для определения жизни это более прямой
        // признак, чем реплицируемый флаг controller.m_bPawnIsAlive: последний
        // может кратко отставать при spawn/смене pawn.
        bool IsPawnAlive() const {
            return pawn != 0 && health > 0 && health <= 1000 && lifeState == 0;
        }

        // Строгий вариант для ragebot: сохраняем controller alive как
        // дополнительный anti-stale фильтр.
        bool IsAlive() const {
            return controller != 0 && controllerAlive && IsValidPlayerHandle(pawnHandle) &&
                   IsPawnAlive();
        }

        bool HasAimPosition() const {
            return sceneNode != 0 && !dormant && IsUsablePlayerOrigin(origin);
        }

        bool IsTargetable() const {
            return IsAlive() && HasAimPosition() && !immune;
        }
    };

    inline PlayerSnapshot ReadPlayerSlot(uintptr_t entityList, int slot) {
        PlayerSnapshot out{};
        out.slot = slot;
        if (!entityList || slot < kFirstPlayerSlot || slot > kMaxPlayerSlots)
            return out;

        out.controller = GetEntityByIndex(entityList, static_cast<uint32_t>(slot));
        if (!out.controller)
            return out;

        const auto& off = offsets::g;
        out.controllerAlive =
            mem::Read<uint8_t>(out.controller + off.m_bPawnIsAlive) != 0;

        // m_hPlayerPawn из CCSPlayerController бывает пустым на живом
        // controller в актуальных клиентах. Базовый m_hPawn содержит ту же
        // связь и является корректным fallback, а не эвристикой по памяти.
        out.pawnHandle = mem::Read<uint32_t>(out.controller + off.m_hPlayerPawn);
        if (IsValidPlayerHandle(out.pawnHandle))
            out.pawn = GetEntityByHandle(entityList, out.pawnHandle);

        if (!out.pawn) {
            const uint32_t basePawnHandle = mem::Read<uint32_t>(out.controller + off.m_hPawn);
            if (IsValidPlayerHandle(basePawnHandle)) {
                out.pawnHandle = basePawnHandle;
                out.pawn = GetEntityByHandle(entityList, out.pawnHandle);
            }
        }
        if (!out.pawn)
            return out;

        out.team = mem::Read<uint8_t>(out.pawn + off.m_iTeamNum);
        out.health = mem::Read<int>(out.pawn + off.m_iHealth);
        out.lifeState = mem::Read<uint8_t>(out.pawn + off.m_lifeState);
        out.immune = mem::Read<uint8_t>(out.pawn + off.m_bGunGameImmunity) != 0;
        out.velocity = mem::Read<Vector3>(out.pawn + off.m_vecAbsVelocity);
        out.sceneNode = mem::Read<uintptr_t>(out.pawn + off.m_pGameSceneNode);
        if (out.sceneNode) {
            out.dormant = mem::Read<uint8_t>(out.sceneNode + off.m_bDormant) != 0;
            out.origin = mem::Read<Vector3>(out.sceneNode + off.m_vecAbsOrigin);
        }
        return out;
    }

    // Локальный pawn уже получен из dwLocalPlayerPawn и не должен зависеть от
    // controller-схемы. Иначе один устаревший m_hPlayerPawn/m_bPawnIsAlive
    // выключает вообще весь feature loop, включая независимые функции камеры.
    inline bool IsPawnAlive(uintptr_t pawn) {
        if (!pawn)
            return false;
        const int health = mem::Read<int>(pawn + offsets::g.m_iHealth);
        const uint8_t lifeState = mem::Read<uint8_t>(pawn + offsets::g.m_lifeState);
        return health > 0 && health <= 1000 && lifeState == 0;
    }

    // Повторная строгая проверка именно УДАЛЁННОЙ цели перед использованием/
    // выстрелом. Она гарантирует, что controller всё ещё указывает на тот же
    // pawn: после смерти/респавна старый указатель проверку не пройдёт.
    inline bool IsPlayerStillAlive(uintptr_t entityList,
                                   uintptr_t controller,
                                   uintptr_t expectedPawn) {
        if (!entityList || !controller || !expectedPawn)
            return false;

        const auto& off = offsets::g;
        if (mem::Read<uint8_t>(controller + off.m_bPawnIsAlive) == 0)
            return false;

        uint32_t handle = mem::Read<uint32_t>(controller + off.m_hPlayerPawn);
        uintptr_t resolved = IsValidPlayerHandle(handle)
            ? GetEntityByHandle(entityList, handle) : 0;
        if (resolved != expectedPawn) {
            handle = mem::Read<uint32_t>(controller + off.m_hPawn);
            resolved = IsValidPlayerHandle(handle)
                ? GetEntityByHandle(entityList, handle) : 0;
        }
        if (resolved != expectedPawn)
            return false;

        return IsPawnAlive(expectedPawn);
    }

    inline bool IsPlayerStillTargetable(uintptr_t entityList,
                                        uintptr_t controller,
                                        uintptr_t expectedPawn) {
        if (!IsPlayerStillAlive(entityList, controller, expectedPawn))
            return false;

        const auto& off = offsets::g;
        if (mem::Read<uint8_t>(expectedPawn + off.m_bGunGameImmunity) != 0)
            return false;
        const uintptr_t node = mem::Read<uintptr_t>(expectedPawn + off.m_pGameSceneNode);
        if (!node || mem::Read<uint8_t>(node + off.m_bDormant) != 0)
            return false;
        return IsUsablePlayerOrigin(mem::Read<Vector3>(node + off.m_vecAbsOrigin));
    }

    inline bool IsCrosshairOnPawn(uintptr_t localPawn,
                                  uintptr_t entityList,
                                  uintptr_t expectedPawn) {
        if (!localPawn || !entityList || !expectedPawn)
            return false;
        const int index = mem::Read<int>(localPawn + offsets::g.m_iIDEntIndex);
        if (index <= 0 || static_cast<uint32_t>(index) == kInvalidHandleIndex)
            return false;
        return GetEntityByIndex(entityList, static_cast<uint32_t>(index)) == expectedPawn;
    }

    template <typename Fn>
    inline void ForEachPlayer(uintptr_t entityList, Fn&& fn) {
        for (int slot = kFirstPlayerSlot; slot <= kMaxPlayerSlots; ++slot) {
            PlayerSnapshot player = ReadPlayerSlot(entityList, slot);
            if (player.HasController())
                fn(player);
        }
    }

    // Bone cache layout from the supplied CHEAT source. CBoneData is 0x20:
    // Vector3 Location, float Scale, quaternion Rotation. Bone 7 is head.
    inline bool GetBonePosition(uintptr_t pawn, int bone, Vector3& out) {
        if (!pawn || bone < 0 || bone > 255)
            return false;
        const uintptr_t node = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
        if (!node)
            return false;
        const uintptr_t bones = mem::Read<uintptr_t>(node + 0x150 + 0x80);
        if (!bones)
            return false;
        const Vector3 position = mem::Read<Vector3>(bones + static_cast<uintptr_t>(bone) * 0x20);
        if (!IsUsablePlayerOrigin(position))
            return false;
        out = position;
        return true;
    }

    // Глаза павна: abs origin из сцена-ноды + view offset.
    // m_vecViewOffset (C_BaseModelEntity, 0xE78) — тип CNetworkViewOffsetVector,
    // сетевой: при несовпадении билда/оффсета отдаёт не Vector, а мусор.
    inline Vector3 GetEyePosition(uintptr_t pawn) {
        Vector3 origin{};
        const uintptr_t sceneNode = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
        if (sceneNode)
            origin = mem::Read<Vector3>(sceneNode + offsets::g.m_vecAbsOrigin);

        Vector3 viewOffset = mem::Read<Vector3>(pawn + offsets::g.m_vecViewOffset);
        if (!std::isfinite(viewOffset.x) || viewOffset.x < -100.0f || viewOffset.x > 100.0f) viewOffset.x = 0.0f;
        if (!std::isfinite(viewOffset.y) || viewOffset.y < -100.0f || viewOffset.y > 100.0f) viewOffset.y = 0.0f;
        if (!std::isfinite(viewOffset.z) || viewOffset.z < -200.0f || viewOffset.z > 300.0f) viewOffset.z = 64.0f;

        return origin + viewOffset;
    }

    // Команда pawn хранится как uint8_t.
    inline uint8_t GetTeam(uintptr_t pawn) {
        return mem::Read<uint8_t>(pawn + offsets::g.m_iTeamNum);
    }

    // Углы из точки A в точку B. Конвенция Source: pitch вверх отрицательный,
    // yaw из atan2 уже лежит в [-180, 180].
    inline Vector2 CalcAngles(const Vector3& from, const Vector3& to) {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float dz = to.z - from.z;
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
