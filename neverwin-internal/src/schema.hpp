#pragma once
// ============================================================================
// Source2 Schema — runtime-поиск оффсетов по ИМЕНИ класса и поля.
//
// Портировано из velocity-cs2 (123abc.zip): core/systems/impl/schemas.cpp.
// Суть: CSchemaSystem (CreateInterface "SchemaSystem_001") отдаёт типоскоп
// модуля, в нём ищем класс по имени, потом его поля (шаг 0x20: имя @0,
// оффсет @0x10) и сверяем FNV-1a хэш имени поля.
//
// Зачем: жёсткие schema-оффсеты (m_iHealth, m_hPawn, m_vecViewOffset...)
// меняются Valve без предупреждения — как это уже случалось с m_hPawn
// (0x6BC -> 0x600). Имена полей стабильны. Если schema-поиск вернул 0
// (система не поднята / имя сменилось) — вызывающий должен упасть на
// зашитый fallback (см. Off()).
// ============================================================================
#include "memory.hpp"

#include <cstdint>
#include <cstring>

namespace fnv1a {
    // FNV-1a 32 — ровно как в velocity (seed 2166136261, prime 16777619).
    constexpr uint32_t hash(const char* str, size_t length) noexcept {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < length; ++i) {
            h ^= static_cast<uint32_t>(str[i]);
            h *= 16777619u;
        }
        return h;
    }

    inline uint32_t runtime_hash(const char* str) noexcept {
        if (!str) return 0;
        uint32_t h = 2166136261u;
        while (*str) {
            h ^= static_cast<uint32_t>(*str++);
            h *= 16777619u;
        }
        return h;
    }
}

constexpr uint32_t operator""_hash(const char* str, size_t length) noexcept {
    return fnv1a::hash(str, length);
}

// Вызов виртуальной функции по vtable (внутренний чит — прямой вызов).
template <typename T, typename... Args>
[[nodiscard]] inline T schema_call_vfunc(uintptr_t instance, size_t index, Args... args) {
    if (!instance)
        return T{};
    const uintptr_t vtable = mem::Read<uintptr_t>(instance);
    if (!vtable || !mem::IsValidPtr(reinterpret_cast<const void*>(vtable), 0x10))
        return T{};
    const uintptr_t func = mem::Read<uintptr_t>(vtable + index * sizeof(uintptr_t));
    if (!func || !mem::IsValidPtr(reinterpret_cast<const void*>(func), 8))
        return T{};
    using Fn = T(__fastcall*)(uintptr_t, Args...);
    return reinterpret_cast<Fn>(func)(instance, static_cast<Args>(args)...);
}

namespace schema {

    // CreateInterface(module, name) через регистр интерфейсов tier0:
    // create_interface+3 = disp к началу списка {create_fn, name, next}.
    inline uintptr_t GetModuleInterface(const wchar_t* module, const char* name) {
        const HMODULE mod = GetModuleHandleW(module);
        if (!mod) return 0;
        const auto ci = reinterpret_cast<uintptr_t>(
            GetProcAddress(reinterpret_cast<HMODULE>(mod), "CreateInterface"));
        if (!ci) return 0;
        if (!mem::IsValidPtr(reinterpret_cast<const void*>(ci), 16)) return 0;

        const int32_t disp = mem::Read<int32_t>(ci + 3);
        const uintptr_t list = mem::Read<uintptr_t>(ci + 7 + disp);
        if (!list || !mem::IsValidPtr(reinterpret_cast<const void*>(list), 0x20))
            return 0;

        uintptr_t current = list;
        for (int guard = 0; guard < 256 && current; ++guard) {
            // interface_reg_t { create_fn, name, next }
            const uintptr_t create_fn = mem::Read<uintptr_t>(current);
            const char* iface_name = reinterpret_cast<const char*>(mem::Read<uintptr_t>(current + 8));
            if (!create_fn || !mem::IsValidPtr(reinterpret_cast<const void*>(create_fn), 8)) {
                break;
            }
            if (iface_name && std::strcmp(iface_name, name) == 0)
                return reinterpret_cast<uintptr_t>(reinterpret_cast<void* (*)()>(create_fn)());
            current = mem::Read<uintptr_t>(current + 16);
        }
        return 0;
    }

    // SchemaSystem_001 — в CS2 отдаёт schemasystem.dll; на всякий
    // случай пробуют client/engine2 следом.
    inline uintptr_t SchemaSystem() {
        static uintptr_t cached = 0;
        if (cached) return cached;
        static const wchar_t* kModules[] = { L"schemasystem.dll", L"client.dll", L"engine2.dll" };
        for (const wchar_t* m : kModules) {
            cached = GetModuleInterface(m, "SchemaSystem_001");
            if (cached) break;
        }
        return cached;
    }

    // vtable[13](typeScopeSystem, module_name, nullptr) -> TypeScope* модуля.
    inline uintptr_t TypeScope(const char* module_name) {
        const uintptr_t sys = SchemaSystem();
        if (!sys) return 0;
        return schema_call_vfunc<uintptr_t>(sys, 13, module_name, (uintptr_t)nullptr);
    }

    // lookup(class_name, field_hash) -> оффсет поля или 0.
    // class_info: +0x24 = число полей (u16), +0x30 = указатель на массив
    // полей, поле = { name*, pad, offset(u32) @0x10, ... } с шагом 0x20.
    inline uint32_t Lookup(const char* class_name, uint32_t field_hash) {
        static uintptr_t typeScope = 0;
        if (!typeScope)
            typeScope = TypeScope("client.dll");
        if (!typeScope)
            return 0;

        uintptr_t class_info = 0;
        schema_call_vfunc<void>(typeScope, 2, &class_info, class_name);
        if (!class_info || !mem::IsValidPtr(reinterpret_cast<const void*>(class_info), 0x40)) {
            // Типоскоп мог устать (редко) — сбрасываем и даём шанс новому.
            typeScope = 0;
            return 0;
        }

        const uintptr_t fields = mem::Read<uintptr_t>(class_info + 0x30);
        const uint16_t count = mem::Read<uint16_t>(class_info + 0x24);
        if (!fields || !count || count > 4096)
            return 0;

        for (uint16_t i = 0; i < count; ++i) {
            const uintptr_t field_addr = fields + static_cast<uintptr_t>(i) * 0x20;
            const char* name_ptr = reinterpret_cast<const char*>(mem::Read<uintptr_t>(field_addr));
            if (!name_ptr || !mem::IsValidPtr(reinterpret_cast<const void*>(name_ptr), 64))
                continue;
            if (fnv1a::runtime_hash(name_ptr) == field_hash)
                return mem::Read<uint32_t>(field_addr + 0x10);
        }
        return 0;
    }

    // Оффсет поля с fallback на зашитое значение: schema вернула 0
    // (не поднята / билд, где имени нет) — используем offsets::g.
    inline uint32_t Off(const char* class_name, uint32_t field_hash, uint32_t fallback) {
        const uint32_t v = Lookup(class_name, field_hash);
        return v ? v : fallback;
    }
}

// Кэшируемый lookup (как в velocity) с ретраями: если SchemaSystem ещё не
// поднят на момент первого вызова (инжект во время загрузки клиента),
// пробуем снова раз в 5 с до 30 попыток (~2.5 мин), потом — зашиваем 0
// и вызывающий живёт на fallback-оффсете.
#define SCHEMA(class_name, field_hash) \
    ([]() -> uint32_t { \
        struct Cache { uint32_t val = 0; DWORD at = 0; int tries = 0; }; \
        static Cache c{}; \
        if (!c.val) { \
            const DWORD nowTick = GetTickCount(); \
            if (c.tries < 30 && (!c.at || nowTick - c.at >= 5000)) { \
                c.at = nowTick; \
                ++c.tries; \
                c.val = schema::Lookup(class_name, field_hash); \
            } \
        } \
        return c.val; \
    }())

#define SCHEMA_OFF(class_name, field_hash, fallback) \
    ([]() -> uint32_t { \
        struct Cache { uint32_t val = 0; DWORD at = 0; int tries = 0; }; \
        static Cache c{}; \
        if (!c.val) { \
            const DWORD nowTick = GetTickCount(); \
            if (c.tries < 30 && (!c.at || nowTick - c.at >= 5000)) { \
                c.at = nowTick; \
                ++c.tries; \
                c.val = schema::Lookup(class_name, field_hash); \
            } \
        } \
        return c.val ? c.val : (fallback); \
    }())
