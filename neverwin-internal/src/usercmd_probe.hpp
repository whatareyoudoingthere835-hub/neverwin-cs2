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
        uintptr_t stringCopy = 0;
        uintptr_t serializeMoveCrc = 0;
        uintptr_t computeRandomSeed = 0;
        uintptr_t calculateSpread = 0;

        bool ReadyForRead() const { return getUserCmd && getUserCmdBase; }
        bool ReadyForApply() const { return ReadyForRead() && stringCopy && serializeMoveCrc; }
        bool ReadyForNoSpread() const { return computeRandomSeed && calculateSpread; }
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
        uintptr_t command = 0;
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
        // Ghidra current-build path: FUN_180B0AF10 calls FUN_180902B00
        // immediately after reading sequence from the base at +0x5910.
        // This is the sibling GetUserCmd(localController, sequence).
        if (patterns.getUserCmd) {
            using GetUserCmdFn = uintptr_t(__fastcall*)(uintptr_t, int);
            out.command = reinterpret_cast<GetUserCmdFn>(patterns.getUserCmd)(localController, sequence);
        }
        out.valid = true;
        return out;
    }

    struct InputCandidate {
        uintptr_t address = 0;
        int commandNumber = 0;
        uintptr_t commandRing = 0;
        uintptr_t currentCmd = 0;
        float inputPitch = 0.0f;
        float inputYaw = 0.0f;
        float inputAngleDelta = 99999.0f;
        float commandPitch = 0.0f;
        float commandYaw = 0.0f;
        float commandAngleDelta = 99999.0f;
    };

    struct InputProbe {
        uintptr_t input = 0;
        uintptr_t vtable = 0;
        uintptr_t methods[8]{};
        uintptr_t inputPattern = 0;
        uintptr_t relatedCall = 0;
        uintptr_t createMovePattern = 0;
        InputCandidate candidates[3]{};
        int commandNumber = 0;
        uintptr_t commandRing = 0;
        uintptr_t currentCmd = 0;
        float commandPitch = 0.0f;
        float commandYaw = 0.0f;
        float angleDelta = 99999.0f;
        bool valid = false;
    };

    inline InputProbe ProbeCSGOInput(uintptr_t clientBase, uintptr_t inputOffset, float livePitch, float liveYaw) {
        InputProbe out{};
        // dwCSGOInput from the currently loaded dumper offsets. Read-only only.
        out.input = mem::Read<uintptr_t>(clientBase + inputOffset);
        // This object is not a conventional vtable object on the current
        // client. Ghidra shows direct calls through [CCSGOInput + 0x8], so
        // record the first object slots themselves (no second dereference).
        if (out.input) {
            out.vtable = mem::Read<uintptr_t>(out.input);
            for (int i = 0; i < 8; ++i)
                out.methods[i] = mem::Read<uintptr_t>(out.input + sizeof(uintptr_t) * i);
            out.valid = out.methods[0] != 0;
        }

        // cs2-sdk header has build 14175. Compare all plausible object roots
        // read-only against live view angles before trusting its B50/B58 fields.
        const uintptr_t directGlobal = clientBase + inputOffset;
        const uintptr_t legacyStatic = clientBase + 0x23B95F0;
        const uintptr_t roots[] = { out.input, directGlobal, legacyStatic };
        auto angleDelta = [&](float pitch, float yaw) {
            float dy = std::fabs(yaw - liveYaw);
            while (dy > 360.0f) dy -= 360.0f;
            if (dy > 180.0f) dy = 360.0f - dy;
            return std::fabs(pitch - livePitch) + dy;
        };
        for (int n = 0; n < 3; ++n) {
            auto& candidate = out.candidates[n];
            candidate.address = roots[n];
            if (!candidate.address) continue;
            candidate.inputPitch = mem::Read<float>(candidate.address + 0x688);
            candidate.inputYaw = mem::Read<float>(candidate.address + 0x68C);
            candidate.inputAngleDelta = angleDelta(candidate.inputPitch, candidate.inputYaw);
            candidate.commandNumber = mem::Read<int>(candidate.address + 0xB50);
            candidate.commandRing = mem::Read<uintptr_t>(candidate.address + 0xB58);
            if (candidate.commandRing && candidate.commandNumber > 0 && candidate.commandNumber < 10000000) {
                constexpr int kCommandRingSize = 150;
                constexpr uintptr_t kCommandStride = 0x440;
                const int index = candidate.commandNumber % kCommandRingSize;
                candidate.currentCmd = candidate.commandRing + static_cast<uintptr_t>(index) * kCommandStride;
                candidate.commandPitch = mem::Read<float>(candidate.currentCmd + 0x18);
                candidate.commandYaw = mem::Read<float>(candidate.currentCmd + 0x1C);
                candidate.commandAngleDelta = angleDelta(candidate.commandPitch, candidate.commandYaw);
            }
        }
        // Preserve candidate 0 in legacy fields for existing diagnostics.
        out.commandNumber = out.candidates[0].commandNumber;
        out.commandRing = out.candidates[0].commandRing;
        out.currentCmd = out.candidates[0].currentCmd;
        out.commandPitch = out.candidates[0].commandPitch;
        out.commandYaw = out.candidates[0].commandYaw;
        out.angleDelta = out.candidates[0].commandAngleDelta;

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

    struct ButtonProbe {
        uintptr_t cmd = 0;
        uint32_t offsets[19]{};
        uint64_t values[19]{};
        int count = 0;
    };

    // Read-only first pass: while physical SPACE is held, locate qword fields
    // in the validated 0x98 CUserCmd that carry IN_JUMP (bit 1).
    inline ButtonProbe FindDirectJumpBits(uintptr_t cmd) {
        ButtonProbe out{};
        out.cmd = cmd;
        if (!cmd) return out;
        for (uint32_t offset = 0; offset <= 0x90; offset += 8) {
            const uint64_t value = mem::Read<uint64_t>(cmd + offset);
            if ((value & 0x2ull) != 0 && out.count < 19) {
                out.offsets[out.count] = offset;
                out.values[out.count] = value;
                ++out.count;
            }
        }
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
        // Current build disassembly confirms sibling at -0x90: caller passes
        // (local controller, sequence) and receives CUserCmd in RAX.
        if (!out.getUserCmd && out.getUserCmdBase)
            out.getUserCmd = out.getUserCmdBase - 0x90;
        if (const uintptr_t found = FindBytes(client, kSubtickAlloc, kSubtickAllocMask, sizeof(kSubtickAlloc)))
            out.subtickMoveAlloc = ResolveRelativeCall(found + 16);
        if (const uintptr_t found = FindBytes(client, kUtlPush, kUtlPushMask, sizeof(kUtlPush)))
            out.utlVectorPush = ResolveRelativeCall(found);

        // Fixed Velocity V16 input.apply() helpers.
        static const uint8_t kStringCopy[] = { 0xE8,0,0,0,0,0x0F,0x10,0x45,0x88 };
        static const char kStringCopyMask[] = "x????xxxx";
        static const uint8_t kSerializeCrc[] = {
            0x48,0x89,0x5C,0x24,0,0x55,0x56,0x57,0x48,0x83,0xEC,0x30,
            0x49,0x8B,0xC0,0x48,0x8B,0xFA,0x48,0x8B,0xF1,0x48,0x8B,0x09,0xF6,0xC1,0x03
        };
        static const char kSerializeCrcMask[] = "xxxx?xxxxxxxxxxxxxxxxxxxxxx";
        if (const uintptr_t found = FindBytes(client, kStringCopy, kStringCopyMask, sizeof(kStringCopy)))
            out.stringCopy = ResolveRelativeCall(found);
        out.serializeMoveCrc = FindBytes(client, kSerializeCrc, kSerializeCrcMask, sizeof(kSerializeCrc));

        // NoSpread helpers (signatures from the UGame NoSpread source, "updated" marks).
        // ComputeRandomSeed: client.dll "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 48 8B F9 41 8B ?"
        static const uint8_t kComputeSeed[] = {
            0x48,0x89,0x5C,0x24,0,0x57,0x48,0x81,0xEC,0,0,0,0,0x48,0x8B,0xF9,
            0x41,0x8B,0
        };
        static const char kComputeSeedMask[] = "xxxx?xxxx????xxx xx";
        out.computeRandomSeed = FindBytes(client, kComputeSeed, kComputeSeedMask, sizeof(kComputeSeed));
        // CalculateSpread: client.dll "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 63 EA"
        static const uint8_t kCalcSpread[] = {
            0x48,0x8B,0xC4,0x48,0x89,0x58,0,0x48,0x89,0x68,0,0x48,0x89,0x70,0,0x57,
            0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0,0,0,0,0x4C,0x63,0xEA
        };
        static const char kCalcSpreadMask[] = "xxxxxx?xxxxx?xxxx xxxxxxxx????xxxxxxxxx";
        out.calculateSpread = FindBytes(client, kCalcSpread, kCalcSpreadMask, sizeof(kCalcSpread));
        return out;
    }
} // namespace usercmd_probe
