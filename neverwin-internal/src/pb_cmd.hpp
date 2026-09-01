#pragma once
#include "usercmd_probe.hpp"

// Protobuf command layer rebuilt from the official PB headers
// (cspatterns.dev/CUserCMD, build-era 1417x). Every message object begins
// with PBMessage { vtable, metadata } = 0x10 bytes, then has_bits/cached_size.
//
//   CSGOUserCmdPB : PBMessage, has_bits, cached_size,
//                   repeated input_history, CBaseUserCmdPB* base
//   CBaseUserCmdPB: PBMessage, has_bits, cached_size,
//                   repeated subtick_moves, move_crc*, buttons_pb*,
//                   viewangles* (CMsgQAngle), execution_notes*, ...
//   CMsgQAngle    : PBMessage, has_bits, cached_size, x, y, z
//   CSubtickMoveStep: PBMessage, has_bits, cached_size,
//                   button, pressed, when, analog_forward/left, pitch/yaw
namespace pbcmd {
    constexpr uintptr_t kMsgHeader = 0x10; // PBMessage vtable+metadata
    constexpr uintptr_t kImplHeader = 0x10; // impl pointer adjustment (velocity impl_ptr)

    // has_bits для CBaseUserCmdPB
    constexpr uint32_t kBaseMoveCrc = 0x1;
    constexpr uint32_t kBaseButtons = 0x2;
    constexpr uint32_t kBaseViewangles = 0x4;
    // has_bits для CSubtickMoveStep
    constexpr uint32_t kStepAll = 0x3F;

    // Raw (protobuf object) pointer -> implementation struct base.
    inline uintptr_t Impl(uintptr_t raw) { return raw ? raw + kImplHeader : 0; }

    struct UserCmdView {
        uintptr_t cmd = 0;        // validated CUserCmd (0x98 stride ring)
        uintptr_t csgo = 0;       // CSGOUserCmdPB impl
        uintptr_t base = 0;       // CBaseUserCmdPB impl
        uintptr_t buttons = 0;    // CInButtonStatePB impl
        uintptr_t viewangles = 0; // CMsgQAngle impl
        uintptr_t subticks = 0;   // repeated subtick_moves field
        bool ok = false;
    };

    // usercmd: CSGOUserCmdPB начинается на +0x20, m_base (+0x20 в структуре)
    // => raw base-указатель на cmd+0x40 (совпадает с нашим подтверждённым
    // GetBaseImpl). buttons_pb на base impl +0x28, viewangles на +0x30,
    // repeated subtick_moves на +0x08.
    inline UserCmdView Open(uintptr_t cmd) {
        UserCmdView v;
        v.cmd = cmd;
        if (!cmd)
            return v;
        const uintptr_t baseRaw = *reinterpret_cast<const uintptr_t*>(cmd + 0x40);
        if (!baseRaw)
            return v;
        v.csgo = cmd + 0x20;
        v.base = baseRaw + kImplHeader;
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(v.base), 0x48))
            return v;
        const uintptr_t buttonsRaw = *reinterpret_cast<const uintptr_t*>(v.base + 0x28);
        if (buttonsRaw)
            v.buttons = buttonsRaw + kImplHeader;
        const uintptr_t anglesRaw = *reinterpret_cast<const uintptr_t*>(v.base + 0x30);
        if (anglesRaw)
            v.viewangles = anglesRaw + kImplHeader;
        v.subticks = v.base + 0x08;
        v.ok = true;
        return v;
    }

    inline bool Valid(uintptr_t p, size_t size) {
        return p && mem::IsValidPtr(reinterpret_cast<const void*>(p), size);
    }

    // --- Silent aim: запись углов в protobuf viewangles команды ---
    // Возвращает false, если viewangles-сообщение ещё не создано игрой.
    inline bool WriteViewAngles(uintptr_t cmd, float pitch, float yaw) {
        UserCmdView v = Open(cmd);
        if (!v.ok || !Valid(v.viewangles, 0x14))
            return false;
        *reinterpret_cast<uint32_t*>(v.viewangles) |= 0x7u; // x|y|z
        *reinterpret_cast<float*>(v.viewangles + 0x08) = pitch;
        *reinterpret_cast<float*>(v.viewangles + 0x0C) = yaw;
        *reinterpret_cast<float*>(v.viewangles + 0x10) = 0.0f;
        *reinterpret_cast<uint32_t*>(v.base) |= kBaseViewangles;
        return true;
    }

    // Чтение текущих protobuf-углов команды (для probe/диагностики).
    inline bool ReadViewAngles(uintptr_t cmd, float& pitch, float& yaw) {
        UserCmdView v = Open(cmd);
        if (!v.ok || !Valid(v.viewangles, 0x14))
            return false;
        pitch = *reinterpret_cast<const float*>(v.viewangles + 0x08);
        yaw = *reinterpret_cast<const float*>(v.viewangles + 0x0C);
        return true;
    }

    // --- Кнопки ---
    inline bool WriteButtons(uintptr_t cmd, uint64_t s1, uint64_t s2, uint64_t s3) {
        UserCmdView v = Open(cmd);
        if (!v.ok || !Valid(v.buttons, 0x20))
            return false;
        *reinterpret_cast<uint32_t*>(v.base) |= kBaseButtons;
        *reinterpret_cast<uint32_t*>(v.buttons) |= 0x7u;
        *reinterpret_cast<uint64_t*>(v.buttons + 0x08) = s1;
        *reinterpret_cast<uint64_t*>(v.buttons + 0x10) = s2;
        *reinterpret_cast<uint64_t*>(v.buttons + 0x18) = s3;
        return true;
    }

    // --- Subtick: acquire + пара jump release/press ---
    inline uintptr_t AcquireStep(uintptr_t field, uintptr_t arenaFallback,
                                 const usercmd_probe::Patterns& p) {
        if (!Valid(field, 0x18))
            return 0;
        const uintptr_t rep = *reinterpret_cast<const uintptr_t*>(field + 0x10);
        const int current = *reinterpret_cast<const int*>(field + 0x08);
        if (rep && Valid(rep, 0x10)) {
            const int allocated = *reinterpret_cast<const int*>(rep);
            if (current >= 0 && current < allocated) {
                const uintptr_t element =
                    *reinterpret_cast<const uintptr_t*>(rep + 0x08 + 8ull * static_cast<unsigned>(current));
                if (element) {
                    *reinterpret_cast<int*>(field + 0x08) = current + 1;
                    const uintptr_t step = element + kImplHeader;
                    if (Valid(step, 0x28)) {
                        memset(reinterpret_cast<void*>(step), 0, 0x28);
                        return step;
                    }
                }
            }
        }
        if (!p.subtickMoveAlloc || !p.utlVectorPush)
            return 0;
        uintptr_t arena = *reinterpret_cast<const uintptr_t*>(field);
        if (!arena)
            arena = arenaFallback;
        if (!arena)
            return 0;
        using AllocFn = void*(__fastcall*)(uintptr_t);
        using PushFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
        void* raw = reinterpret_cast<AllocFn>(p.subtickMoveAlloc)(arena);
        if (!raw)
            return 0;
        reinterpret_cast<PushFn>(p.utlVectorPush)(field, reinterpret_cast<uintptr_t>(raw));
        const uintptr_t step = reinterpret_cast<uintptr_t>(raw) + kImplHeader;
        if (!Valid(step, 0x28))
            return 0;
        memset(reinterpret_cast<void*>(step), 0, 0x28);
        return step;
    }

    inline bool AddJumpSubtickPair(uintptr_t cmd, float releaseWhen, float pressWhen,
                                   const usercmd_probe::Patterns& p) {
        UserCmdView v = Open(cmd);
        if (!v.ok)
            return false;
        uintptr_t arena = *reinterpret_cast<const uintptr_t*>(v.subticks);
        if (!arena)
            arena = *reinterpret_cast<const uintptr_t*>(cmd + 0x18);
        constexpr uint64_t kInJump = 0x2ull;
        const uintptr_t up = AcquireStep(v.subticks, arena, p);
        if (!up) return false;
        *reinterpret_cast<uint32_t*>(up) = kStepAll;
        *reinterpret_cast<uint64_t*>(up + 0x08) = kInJump;
        *reinterpret_cast<uint8_t*>(up + 0x10) = 0;
        *reinterpret_cast<float*>(up + 0x14) = releaseWhen;
        const uintptr_t down = AcquireStep(v.subticks, arena, p);
        if (!down) return false;
        *reinterpret_cast<uint32_t*>(down) = kStepAll;
        *reinterpret_cast<uint64_t*>(down + 0x08) = kInJump;
        *reinterpret_cast<uint8_t*>(down + 0x10) = 1;
        *reinterpret_cast<float*>(down + 0x14) = pressWhen;
        return true;
    }
} // namespace pbcmd
