#pragma once
#include "usercmd_probe.hpp"

// Minimal port of Velocity V16 systems::input::apply() for button state only.
// Hot path: every access is a raw dereference after validating each pointer
// exactly once. Previous version did VirtualProtect on every field write,
// which froze the game in air (~5 FPS).
namespace usercmd_apply {
    // Stage codes reported through the log for a one-time verification.
    enum ApplyStage {
        kStageOk = 0,
        kStageNoCmd = 1,
        kStageNoPatterns = 2,
        kStageNoBase = 3,
        kStageBaseInvalid = 4,
        kStageNoButtons = 5,
        kStageButtonsInvalid = 6,
        kStageNoArena = 7,
        kStageOkNoCrc = 8 // кнопки применены, CRC пропущен (нет arena)
    };

    inline ApplyStage ApplyButtons(uintptr_t cmd, uint64_t state1, uint64_t state2,
                                   uint64_t state3, const usercmd_probe::Patterns& p) {
        if (!cmd)
            return kStageNoCmd;
        if (!p.ReadyForApply())
            return kStageNoPatterns;

        // systems::input::usercmd: CSGOUserCmdPB begins at +0x20; its m_base
        // raw protobuf pointer is +0x20 in that structure => cmd +0x40.
        const uintptr_t baseRaw = *reinterpret_cast<const uintptr_t*>(cmd + 0x40);
        if (!baseRaw)
            return kStageNoBase;
        const uintptr_t base = baseRaw + 0x10; // protobuf implementation
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(base), 0x40))
            return kStageBaseInvalid;

        // CBaseUserCmdPB: m_buttons_pb at implementation +0x28.
        const uintptr_t buttonsRaw = *reinterpret_cast<const uintptr_t*>(base + 0x28);
        if (!buttonsRaw)
            return kStageNoButtons; // allocator path intentionally waits for a later port
        const uintptr_t buttons = buttonsRaw + 0x10;
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(buttons), 0x20))
            return kStageButtonsInvalid;

        // protobuf has_bits: base field #3 = bit 0x2; button states 1..3.
        *reinterpret_cast<uint32_t*>(base) |= 0x2u;
        *reinterpret_cast<uint32_t*>(buttons) |= 0x7u;
        *reinterpret_cast<uint64_t*>(buttons + 0x8) = state1;
        *reinterpret_cast<uint64_t*>(buttons + 0x10) = state2;
        *reinterpret_cast<uint64_t*>(buttons + 0x18) = state3;

        // V16 CRC serialization, limited to buttons and existing viewangles.
        uint8_t buf[64]{};
        uint8_t* out = buf;
        uint8_t buttonSize = 0;
        if (state1) buttonSize += 9;
        if (state2) buttonSize += 9;
        if (state3) buttonSize += 9;
        if (buttonSize) {
            *out++ = 0x1A; *out++ = buttonSize;
            if (state1) { *out++ = 0x09; std::memcpy(out, &state1, 8); out += 8; }
            if (state2) { *out++ = 0x11; std::memcpy(out, &state2, 8); out += 8; }
            if (state3) { *out++ = 0x19; std::memcpy(out, &state3, 8); out += 8; }
        }

        const uintptr_t viewRaw = *reinterpret_cast<const uintptr_t*>(base + 0x30);
        const uintptr_t view = viewRaw ? viewRaw + 0x10 : 0;
        float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
        if (view && mem::IsValidPtr(reinterpret_cast<const void*>(view), 0x14)) {
            pitch = *reinterpret_cast<const float*>(view + 0x8);
            yaw = *reinterpret_cast<const float*>(view + 0xC);
            roll = *reinterpret_cast<const float*>(view + 0x10);
        }
        uint8_t angleSize = 0;
        if (pitch != 0.0f) angleSize += 5;
        if (yaw != 0.0f) angleSize += 5;
        if (roll != 0.0f) angleSize += 5;
        if (angleSize) {
            *out++ = 0x22; *out++ = angleSize;
            if (pitch != 0.0f) { *out++ = 0x0D; std::memcpy(out, &pitch, 4); out += 4; }
            if (yaw != 0.0f) { *out++ = 0x15; std::memcpy(out, &yaw, 4); out += 4; }
            if (roll != 0.0f) { *out++ = 0x1D; std::memcpy(out, &roll, 4); out += 4; }
        }

        *reinterpret_cast<uint32_t*>(base) |= 0x1u;
        // Arena: сначала заголовок protobuf-сообщения (V16), затем
        // usercmd::proto_arena (cmd+0x18) — на 14177 заголовок оказался пуст.
        uintptr_t arena = 0;
        const uintptr_t arenaBits = *reinterpret_cast<const uintptr_t*>(baseRaw + 0x8);
        if (arenaBits) {
            arena = arenaBits & ~uintptr_t(0x3);
            if (arenaBits & 1u)
                arena = *reinterpret_cast<const uintptr_t*>(arena);
        }
        if (!arena)
            arena = *reinterpret_cast<const uintptr_t*>(cmd + 0x18);
        if (!arena)
            return kStageOkNoCrc; // кнопки записаны; CRC без arena пропускаем
        uint8_t message[0x18]{};
        using StringCopyFn = void(__fastcall*)(uintptr_t, uintptr_t, int);
        using SerializeFn = void(__fastcall*)(void*, uintptr_t, uintptr_t);
        reinterpret_cast<StringCopyFn>(p.stringCopy)(reinterpret_cast<uintptr_t>(message),
            reinterpret_cast<uintptr_t>(buf), static_cast<int>(out - buf));
        reinterpret_cast<SerializeFn>(p.serializeMoveCrc)(reinterpret_cast<void*>(base + 0x20),
            reinterpret_cast<uintptr_t>(message), arena);
        return kStageOk;
    }

    // protobuf base impl (base_usercmd_pb) of a validated command, or 0.
    inline uintptr_t GetBaseImpl(uintptr_t cmd) {
        if (!cmd)
            return 0;
        const uintptr_t baseRaw = *reinterpret_cast<const uintptr_t*>(cmd + 0x40);
        if (!baseRaw)
            return 0;
        const uintptr_t base = baseRaw + 0x10;
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(base), 0x48))
            return 0;
        return base;
    }

    // Один subtick-шаг из repeated-поля: сначала пул уже выделенных элементов
    // (V16 acquire_subtick_step), иначе alloc через игровой allocator.
    // Поле (impl base+0x08): arena +0x00, current_size +0x08, total +0x0C,
    // rep +0x10; rep: allocated(int) + elements[] c +0x08.
    inline uintptr_t AcquireSubtickStep(uintptr_t field, uintptr_t arenaFallback,
                                        const usercmd_probe::Patterns& p) {
        if (!field || !mem::IsValidPtr(reinterpret_cast<const void*>(field), 0x18))
            return 0;
        const uintptr_t rep = *reinterpret_cast<const uintptr_t*>(field + 0x10);
        const int current = *reinterpret_cast<const int*>(field + 0x08);
        if (rep && mem::IsValidPtr(reinterpret_cast<const void*>(rep), 0x10)) {
            const int allocated = *reinterpret_cast<const int*>(rep);
            if (current >= 0 && current < allocated) {
                const uintptr_t element =
                    *reinterpret_cast<const uintptr_t*>(rep + 0x08 + 8ull * static_cast<unsigned>(current));
                if (element) {
                    *reinterpret_cast<int*>(field + 0x08) = current + 1;
                    const uintptr_t step = element + 0x10;
                    if (mem::IsValidPtr(reinterpret_cast<const void*>(step), 0x28)) {
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
        const uintptr_t step = reinterpret_cast<uintptr_t>(raw) + 0x10;
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(step), 0x28))
            return 0;
        memset(reinterpret_cast<void*>(step), 0, 0x28);
        return step;
    }

    // Пара subtick-шагов IN_JUMP: release(curtime-frametime) + press(curtime).
    // Семантика when — секунды (из разбора CCSPlayerModernJump::BunnyHope),
    // не фракция тика, как было в старом V16.
    // Шаг (impl): bits +0x00, button +0x08, pressed +0x10, when +0x14,
    // analog_forward +0x18, analog_left +0x1C.
    inline bool AddJumpSubtickPair(uintptr_t cmd, float releaseWhen, float pressWhen,
                                   const usercmd_probe::Patterns& p) {
        const uintptr_t base = GetBaseImpl(cmd);
        if (!base)
            return false;
        const uintptr_t field = base + 0x08; // repeated subtick_move_step
        // Arena для выделения шагов: локальная у поля, затем usercmd::proto_arena.
        uintptr_t arena = *reinterpret_cast<const uintptr_t*>(field);
        if (!arena)
            arena = *reinterpret_cast<const uintptr_t*>(cmd + 0x18);
        constexpr uint64_t kInJump = 0x2ull;
        constexpr uint32_t kStepBits = 0x1Fu; // button|pressed|when|analog*2

        const uintptr_t up = AcquireSubtickStep(field, arena, p);
        if (!up)
            return false;
        *reinterpret_cast<uint32_t*>(up) = kStepBits;
        *reinterpret_cast<uint64_t*>(up + 0x08) = kInJump;
        *reinterpret_cast<uint8_t*>(up + 0x10) = 0; // released
        *reinterpret_cast<float*>(up + 0x14) = releaseWhen;

        const uintptr_t down = AcquireSubtickStep(field, arena, p);
        if (!down)
            return false;
        *reinterpret_cast<uint32_t*>(down) = kStepBits;
        *reinterpret_cast<uint64_t*>(down + 0x08) = kInJump;
        *reinterpret_cast<uint8_t*>(down + 0x10) = 1; // pressed
        *reinterpret_cast<float*>(down + 0x14) = pressWhen;
        return true;
    }
}
