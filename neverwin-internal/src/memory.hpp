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

    // Проверяет, что диапазон [ptr, ptr + size) целиком лежит в одном
    // committed-регионе и не помечен PAGE_NOACCESS / PAGE_GUARD.
    inline bool IsValidPtr(const void* ptr, size_t size) {
        if (ptr == nullptr || size == 0)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        const uintptr_t rangeEnd  = reinterpret_cast<uintptr_t>(ptr) + size;
        return rangeEnd <= regionEnd;
    }

    // Безопасное чтение. Если память невалидна — возвращает T{} (нули).
    // Ни исключений, ни крешей.
    template <typename T>
    inline T Read(uintptr_t addr) {
        if (!IsValidPtr(reinterpret_cast<const void*>(addr), sizeof(T)))
            return T{};
        return *reinterpret_cast<const T*>(addr);
    }

    // Безопасная запись. Если регион read-only (например, секция с viewAngles) —
    // делает его writable. Повторный VirtualProtect для того же адреса
    // не выполняется (кэш последнего региона). Кэш thread_local: теперь
    // пишут ДВА потока (цикл фич и хук CreateMove) — общий кэш был бы гонкой.
    template <typename T>
    inline bool Write(uintptr_t addr, const T& value) {
        if (!IsValidPtr(reinterpret_cast<void*>(addr), sizeof(T)))
            return false;

        static thread_local uintptr_t s_lastAddr = 0;
        static thread_local size_t    s_lastSize = 0;
        if (s_lastAddr != addr || s_lastSize != sizeof(T)) {
            DWORD oldProtect = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), PAGE_READWRITE, &oldProtect))
                return false;
            s_lastAddr = addr;
            s_lastSize = sizeof(T);
        }

        *reinterpret_cast<T*>(addr) = value;
        return true;
    }
}
