#pragma once
#include "pch.h"

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

        if (const uintptr_t found = FindBytes(client, kGetCmd, kGetCmdMask, sizeof(kGetCmd)))
            out.getUserCmd = ResolveRelativeCall(found + 8);
        if (const uintptr_t found = FindBytes(client, kGetCmdBase, kGetCmdBaseMask, sizeof(kGetCmdBase)))
            out.getUserCmdBase = ResolveRelativeCall(found + 4);
        if (const uintptr_t found = FindBytes(client, kSubtickAlloc, kSubtickAllocMask, sizeof(kSubtickAlloc)))
            out.subtickMoveAlloc = ResolveRelativeCall(found + 16);
        if (const uintptr_t found = FindBytes(client, kUtlPush, kUtlPushMask, sizeof(kUtlPush)))
            out.utlVectorPush = ResolveRelativeCall(found);
        return out;
    }
} // namespace usercmd_probe
