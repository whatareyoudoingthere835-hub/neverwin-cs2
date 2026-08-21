#pragma once
#include "pch.h"

// ============================================================================
// Безопасная работа с памятью.
//
// В оригинальном internal.txt чтение/запись шли через сырые макросы
// READ_MEM/WRITE_MEM: любой стухший оффсет или мусорный указатель сразу
// давал access violation и ронял игру. Здесь каждое обращение сначала
// проверяется через VirtualQuery, поэтому битые адреса просто игнорируются.
// ============================================================================
namespace mem {

    // Проверяет, что диапазон [ptr, ptr + size) целиком лежит в committed-памяти
    // и не помечен PAGE_NOACCESS / PAGE_GUARD. В отличие от старой версии,
    // которая требовала уместиться в ОДИН регион (и падала на объектах у края
    // региона — team 0x3E7 / viewOffset 0xE78 часто вылетали за границу),
    // эта версия проходит по цепочке регионов и проверяет каждый.
    inline bool IsValidPtr(const void* ptr, size_t size) {
        if (ptr == nullptr || size == 0)
            return false;

        uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t end = start + size;

        while (start < end) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(start), &mbi, sizeof(mbi)) == 0)
                return false;
            if (mbi.State != MEM_COMMIT)
                return false;
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
                return false;
            // PAGE_READONLY, READWRITE, EXECUTE_READ и т.д. — читаемы.
            // Если регион нечитаем (например, PAGE_NOACCESS уже отсеяли),
            // но защита 0 — тоже невалид.
            if (mbi.Protect == 0)
                return false;

            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (regionEnd <= start) // защита от бесконечного цикла
                return false;
            if (regionEnd > end)
                regionEnd = end;
            start = regionEnd;
        }
        return true;
    }

    // Безопасное чтение. Если память невалидна — возвращает T{} (нули).
    // Двойная защита: сначала VirtualQuery-цепочка, потом SEH (только MSVC) —
    // если между проверкой и чтением регион успели освободить, не уроним игру.
    // На MinGW/Clang (zig) SEH через __try нет — полагаемся на IsValidPtr.
    template <typename T>
    inline T Read(uintptr_t addr) {
        if (addr == 0)
            return T{};
        if (!IsValidPtr(reinterpret_cast<const void*>(addr), sizeof(T)))
            return T{};
#ifdef _MSC_VER
        __try {
            return *reinterpret_cast<const T*>(addr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return T{};
        }
#else
        return *reinterpret_cast<const T*>(addr);
#endif
    }

    // Безопасная запись. Если регион read-only (например, секция с viewAngles) —
    // делает его writable. Повторный VirtualProtect для того же адреса
    // не выполняется (кэш последнего региона). Кэш thread_local: теперь
    // пишут ДВА потока (цикл фич и хук CreateMove) — общий кэш был бы гонкой.
    // Письмо под SEH только на MSVC.
    template <typename T>
    inline bool Write(uintptr_t addr, const T& value) {
        if (addr == 0)
            return false;
        if (!IsValidPtr(reinterpret_cast<void*>(addr), sizeof(T)))
            return false;

        static thread_local uintptr_t s_lastAddr = 0;
        static thread_local size_t    s_lastSize = 0;
        if (s_lastAddr != addr || s_lastSize != sizeof(T)) {
            DWORD oldProtect = 0;
#ifdef _MSC_VER
            __try {
                if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), PAGE_READWRITE, &oldProtect))
                    return false;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
#else
            if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), PAGE_READWRITE, &oldProtect))
                return false;
#endif
            s_lastAddr = addr;
            s_lastSize = sizeof(T);
        }

#ifdef _MSC_VER
        __try {
            *reinterpret_cast<T*>(addr) = value;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        *reinterpret_cast<T*>(addr) = value;
        return true;
#endif
    }
}
