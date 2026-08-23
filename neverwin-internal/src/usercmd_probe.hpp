#pragma once
#include "pch.h"
#include "memory.hpp"

// Минимальный read-only probe usercmd-пути Velocity. На этом этапе мы только
// ищем функции в текущем client.dll и НИЧЕГО не вызываем/не меняем в игре.
namespace usercmd_probe {

    struct Patterns {
        uintptr_t getUserCmd = 0;
        uintptr_t getUserCmdBase = 0;
        uintptr_t subtickMoveAlloc = 0;
        uintptr_t utlVectorPush = 0;

        bool ReadyForRead() const { return getUserCmd && getUserCmdBase; }
    };

    inline size_t ModuleSize(HMODULE module) {
        if (!module) return 0;
        const auto base = reinterpret_cast<const uint8_t*>(module);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.SizeOfImage : 0;
    }

    inline uintptr_t FindBytes(HMODULE module, const uint8_t* bytes, const char* mask, size_t length) {
        const auto base = reinterpret_cast<uintptr_t>(module);
        const size_t size = ModuleSize(module);
        if (!base || !size || length > size) return 0;
        for (size_t i = 0; i + length <= size; ++i) {
            bool match = true;
            for (size_t j = 0; j < length; ++j) {
                if (mask[j] == 'x' && *reinterpret_cast<const uint8_t*>(base + i + j) != bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return base + i;
        }
        return 0;
    }

    inline uintptr_t ResolveRelativeCall(uintptr_t callInstruction) {
        if (!callInstruction || *reinterpret_cast<const uint8_t*>(callInstruction) != 0xE8) return 0;
        const int32_t displacement = *reinterpret_cast<const int32_t*>(callInstruction + 1);
        return callInstruction + 5 + displacement;
    }

    struct RuntimeInfo {
        uintptr_t base = 0;
        int sequence = 0;
        bool valid = false;
    };

    // Velocity calls get_usercmd_base(controller), then reads sequence at
    // +0x5910. This remains read-only and is logged before any future command
    // write is enabled.
    inline RuntimeInfo InspectRuntime(uintptr_t localController, const Patterns& patterns) {
        RuntimeInfo out{};
        if (!localController || !patterns.getUserCmdBase)
            return out;
        using GetUserCmdBaseFn = uintptr_t(__fastcall*)(uintptr_t);
        const uintptr_t base = reinterpret_cast<GetUserCmdBaseFn>(patterns.getUserCmdBase)(localController);
        if (!base || !mem::IsValidPtr(reinterpret_cast<const void*>(base + 0x5910), sizeof(int)))
            return out;
        const int sequence = mem::Read<int>(base + 0x5910);
        if (sequence <= 0 || sequence > 10000000)
            return out;
        out.base = base;
        out.sequence = sequence;
        out.valid = true;
        return out;
    }

    struct InputProbe {
        uintptr_t input = 0;
        uintptr_t vtable = 0;
        uintptr_t methods[8]{};
        uintptr_t inputPattern = 0;
        uintptr_t relatedCall = 0;
        uintptr_t createMovePattern = 0;
        int commandNumber = 0;
        uintptr_t commandRing = 0;
        uintptr_t currentCmd = 0;
        float commandPitch = 0.0f;
        float commandYaw = 0.0f;
        float angleDelta = 99999.0f;
        bool valid = false;
    };

    inline InputProbe ProbeCSGOInput(uintptr_t clientBase, float livePitch, float liveYaw) {
        InputProbe out{};
        // dwCSGOInput from the supplied 20 Aug 2026 dump. Read-only only.
        out.input = mem::Read<uintptr_t>(clientBase + 0x23BFB20);
        if (out.input)
            out.vtable = mem::Read<uintptr_t>(out.input);
        if (out.vtable) {
            for (int i = 0; i < 8; ++i)
                out.methods[i] = mem::Read<uintptr_t>(out.vtable + sizeof(uintptr_t) * i);
            out.valid = out.methods[0] != 0;
        }

        // cs2-sdk build 14175 layout, probed against the current object from
        // dwCSGOInput. This is still read-only: a matching view angle proves
        // the ring/stride before future code ever touches buttons.
        if (out.input) {
            out.commandNumber = mem::Read<int>(out.input + 0xB50);
            out.commandRing = mem::Read<uintptr_t>(out.input + 0xB58);
            if (out.commandRing && out.commandNumber > 0) {
                constexpr int kCommandRingSize = 150;
                constexpr uintptr_t kCommandStride = 0x440;
                const int index = out.commandNumber % kCommandRingSize;
                out.currentCmd = out.commandRing + static_cast<uintptr_t>(index) * kCommandStride;
                out.commandPitch = mem::Read<float>(out.currentCmd + 0x18);
                out.commandYaw = mem::Read<float>(out.currentCmd + 0x1C);
                float yawDelta = std::fabs(out.commandYaw - liveYaw);
                while (yawDelta > 360.0f) yawDelta -= 360.0f;
                if (yawDelta > 180.0f) yawDelta = 360.0f - yawDelta;
                out.angleDelta = std::fabs(out.commandPitch - livePitch) + yawDelta;
            }
        }

        const HMODULE client = GetModuleHandleW(L"client.dll");
        if (!client) return out;
        static const uint8_t kInputPattern[] = {
            0x48,0x8B,0x0D,0,0,0,0,0xE8,0,0,0,0,0x48,0x8B,0xCF,0x4C,0x8B,0xF8
        };
        static const char kInputMask[] = "xxx????x????xxxxxx";
        static const uint8_t kRelatedCall[] = {
            0xE8,0,0,0,0,0x48,0x8B,0x0D,0,0,0,0,0x45,0x33,0xE4,0x48,0x89,0x44,0x24
        };
        static const char kRelatedCallMask[] = "x????xxx????xxxxxxx";
        out.inputPattern = FindBytes(client, kInputPattern, kInputMask, sizeof(kInputPattern));
        if (const uintptr_t found = FindBytes(client, kRelatedCall, kRelatedCallMask, sizeof(kRelatedCall)))
            out.relatedCall = ResolveRelativeCall(found);

        // Raw Velocity CreateMove locator. Its custom +28~ decode is not
        // applied yet: v42 reports only the match address before any hook.
        static const uint8_t kCreateMove[] = {
            0xFF,0xFF,0xFF,0xFF,0x48,0x8D,0x05,0,0,0,0,0x48,0x89,0x0D,0,0,0,0
        };
        static const char kCreateMoveMask[] = "xxxxxxx????xxx????";
        out.createMovePattern = FindBytes(client, kCreateMove, kCreateMoveMask, sizeof(kCreateMove));
        return out;
    }

    inline Patterns Scan() {
        Patterns out{};
        const HMODULE client = GetModuleHandleW(L"client.dll");
        if (!client) return out;

        // Fresh Velocity (31 Jul 2026), where '>' points at E8 whose target is
        // the actual internal function address.
        static const uint8_t kGetCmd[] = {
            0x80,0x00,0x00,0x00,0x49,0x8B,0x4F,0x10,0xE8,0,0,0,0,0x48,0x85,0xC0,0x74,0x19
        };
        static const char kGetCmdMask[] = "xxxxxxxxx????xxxxx";
        static const uint8_t kGetCmdBase[] = {
            0x48,0x83,0xEC,0x28,0xE8,0,0,0,0,0x8B,0x80,0x10,0x59,0x00,0x00
        };
        static const char kGetCmdBaseMask[] = "xxxxx????xxxxxx";
        static const uint8_t kSubtickAlloc[] = {
            0x48,0x8B,0x54,0xCA,0x08,0x8D,0x41,0x01,0x89,0x47,0x08,0xEB,0x16,0x48,0x8B,0x0F,0xE8,0,0,0,0,0x48,0x8B,0xD0,0x48,0x8B,0xCF
        };
        static const char kSubtickAllocMask[] = "xxxxxxxxxxxxxxxxx????xxxxxxx";
        static const uint8_t kUtlPush[] = { 0xE8,0,0,0,0,0x4C,0x8B,0xD0,0x45,0x8B,0x4A,0x10 };
        static const char kUtlPushMask[] = "x????xxxxxxx";

        if (const uintptr_t found = FindBytes(client, kGetCmd, kGetCmdMask, sizeof(kGetCmd))) {
            out.getUserCmd = ResolveRelativeCall(found + 8);
        } else {
            // Valve changed the four bytes before the stable RCX load in the
            // current client. Keep the identifying call context, but only use
            // this fallback for read-only probing until runtime validation.
            static const uint8_t kGetCmdRelaxed[] = {
                0x49,0x8B,0x4F,0x10,0xE8,0,0,0,0,0x48,0x85,0xC0,0x74,0x19
            };
            static const char kGetCmdRelaxedMask[] = "xxxxx????xxxxx";
            if (const uintptr_t found = FindBytes(client, kGetCmdRelaxed, kGetCmdRelaxedMask,
                                                   sizeof(kGetCmdRelaxed)))
                out.getUserCmd = ResolveRelativeCall(found + 4);
        }
        if (const uintptr_t found = FindBytes(client, kGetCmdBase, kGetCmdBaseMask, sizeof(kGetCmdBase)))
            out.getUserCmdBase = ResolveRelativeCall(found + 4);
        if (const uintptr_t found = FindBytes(client, kSubtickAlloc, kSubtickAllocMask, sizeof(kSubtickAlloc)))
            out.subtickMoveAlloc = ResolveRelativeCall(found + 16);
        if (const uintptr_t found = FindBytes(client, kUtlPush, kUtlPushMask, sizeof(kUtlPush)))
            out.utlVectorPush = ResolveRelativeCall(found);
        return out;
    }
} // namespace usercmd_probe
