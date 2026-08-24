#pragma once
#include "usercmd_probe.hpp"

// Minimal port of Velocity V16 systems::input::apply() for button state only.
// Layout is validated by our CUserCmd +0x60/+0x68 probes before use.
namespace usercmd_apply {
    inline bool ApplyButtons(uintptr_t cmd, uint64_t state1, uint64_t state2,
                             uint64_t state3, const usercmd_probe::Patterns& p) {
        if (!cmd || !p.ReadyForApply())
            return false;

        // systems::input::usercmd: CSGOUserCmdPB begins at +0x20; its m_base
        // raw protobuf pointer is +0x20 in that structure => cmd +0x40.
        const uintptr_t baseRaw = mem::Read<uintptr_t>(cmd + 0x40);
        if (!baseRaw)
            return false;
        const uintptr_t base = baseRaw + 0x10; // protobuf implementation
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(base), 0x40))
            return false;

        // CBaseUserCmdPB: m_buttons_pb at implementation +0x28.
        const uintptr_t buttonsRaw = mem::Read<uintptr_t>(base + 0x28);
        if (!buttonsRaw)
            return false; // allocator path intentionally waits for a later port
        const uintptr_t buttons = buttonsRaw + 0x10;
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(buttons), 0x20))
            return false;

        // protobuf has_bits: base field #3 = bit 0x2; button states 1..3.
        mem::Write<uint32_t>(base, mem::Read<uint32_t>(base) | 0x2u);
        mem::Write<uint32_t>(buttons, mem::Read<uint32_t>(buttons) | 0x7u);
        if (!mem::Write<uint64_t>(buttons + 0x8, state1) ||
            !mem::Write<uint64_t>(buttons + 0x10, state2) ||
            !mem::Write<uint64_t>(buttons + 0x18, state3))
            return false;

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

        const uintptr_t viewRaw = mem::Read<uintptr_t>(base + 0x30);
        const uintptr_t view = viewRaw ? viewRaw + 0x10 : 0;
        const float pitch = mem::Read<float>(view + 0x8);
        const float yaw = mem::Read<float>(view + 0xC);
        const float roll = mem::Read<float>(view + 0x10);
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

        mem::Write<uint32_t>(base, mem::Read<uint32_t>(base) | 0x1u);
        const uintptr_t arenaBits = mem::Read<uintptr_t>(baseRaw + 0x8);
        uintptr_t arena = arenaBits & ~uintptr_t(0x3);
        if (arenaBits & 1u)
            arena = mem::Read<uintptr_t>(arena);
        uint8_t message[0x18]{};
        using StringCopyFn = void(__fastcall*)(uintptr_t, uintptr_t, int);
        using SerializeFn = void(__fastcall*)(void*, uintptr_t, uintptr_t);
        reinterpret_cast<StringCopyFn>(p.stringCopy)(reinterpret_cast<uintptr_t>(message),
            reinterpret_cast<uintptr_t>(buf), static_cast<int>(out - buf));
        reinterpret_cast<SerializeFn>(p.serializeMoveCrc)(reinterpret_cast<void*>(base + 0x20),
            reinterpret_cast<uintptr_t>(message), arena);
        return true;
    }
}
