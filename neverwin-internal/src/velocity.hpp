#pragma once
// ============================================================================
// Velocity-паттерны (123abc.zip, velocity-cs2) — портированный набор.
//
// За что:
//  1. PATTERN-фолбэки для наших dw-оффсетов: когда Valve меняет билд,
//     dw-глобалы (dwEntityList, dwViewMatrix, dwCSGOInput...) текут первыми,
//     а паттерны функций живут дольше. Если прочитанное dw-значение
//     подозрительно (0/мусор) — подменяем результатом паттерна (и логим).
//  2. Decode-нотация velocity: "module:BYTES", где
//       >  — resolve relative call (E8+disp) на позиции '>'
//       *  — RIP-relative load на позиции '*'
//       ~  — финальный dereference результата
//       +N / -N — пост-оффсет (hex), применяется до '~'
//       ? / ?? — wildcard
//     Пример: create_move = FFFFFFFF488D05*????????48890D????????+28~
//     = *(input_system_global + 0x28) = функция CreateMove (слот 5 = 5*8).
//     handle_view_angles — тот же пролог, +40~ (слот 8).
//  3. Cvar-ридер (sv_gravity, sv_standable_normal, sv_autobunnyhopping...)
//     через VEngineCvar007 — FNV-хэш имён, значение float/int в +0x58
//     (у clantag это же место было "stringValue" — union).
//
// Паттерны валидируются ЖИВОЙ игрой: скан находит их или нет — и каждый
// адрес дополнительно sanity-ится при использовании. Если паттерн не
// матчится (другой билд) — фолбэк просто не активен.
// ============================================================================
#include "pch.h"
#include "memory.hpp"
#include "log.hpp"
#include "schema.hpp"   // fnv1a

#include <cstring>

namespace velo {

    // --- Разбор и скан паттерна velocity-нотации -------------------------
    struct ParsedPattern {
        uint8_t bytes[128]{};
        bool wildcard[128]{};
        int count = 0;
        int firstByteIdx = -1;
        uint8_t firstByte = 0;
        bool opRelCall = false;     // '>'
        bool opRipLoad = false;     // '*'
        int opByteOffset = 0;
        int64_t postOffset = 0;     // +N / -N
        bool derefFinal = false;    // '~'
        char module[64]{};
    };

    inline int HexVal(char ch) {
        return (ch >= '0' && ch <= '9') ? ch - '0'
             : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
             : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
    }

    inline bool ParsePattern(const char* spec, ParsedPattern& out) {
        const char* colon = std::strchr(spec, ':');
        if (!colon || colon == spec)
            return false;
        const size_t modLen = std::min<size_t>(colon - spec, 63);
        std::memcpy(out.module, spec, modLen);
        out.module[modLen] = 0;
        out.count = 0;
        out.firstByteIdx = -1;

        const char* p = colon + 1;
        while (*p && out.count < 128) {
            const char c = *p;
            if (c == ' ' || c == '\t') { ++p; continue; }
            if (c == '>') { out.opRelCall = true;  out.opByteOffset = out.count; ++p; continue; }
            if (c == '*') { out.opRipLoad = true;  out.opByteOffset = out.count; ++p; continue; }
            if (c == '~') { out.derefFinal = true; ++p; continue; }
            if (c == '+' || c == '-') {
                int64_t v = 0;
                ++p;
                while (*p && *p != ' ' && *p != '\t') {
                    const int hv = HexVal(*p);
                    if (hv < 0) break;
                    v = (v << 4) | hv;
                    ++p;
                }
                out.postOffset += (c == '-') ? -v : v;
                continue;
            }
            if (c == '?') {
                out.bytes[out.count] = 0;
                out.wildcard[out.count] = true;
                ++out.count;
                ++p;
                if (*p == '?') ++p;
                continue;
            }
            const int hi = HexVal(*p);
            if (hi < 0)
                return false;
            uint8_t byte = static_cast<uint8_t>(hi);
            ++p;
            if (*p) {
                const int lo = HexVal(*p);
                if (lo >= 0) {
                    byte = static_cast<uint8_t>((hi << 4) | lo);
                    ++p;
                }
            }
            out.bytes[out.count] = byte;
            out.wildcard[out.count] = false;
            if (out.firstByteIdx < 0)
                out.firstByteIdx = out.count;
            ++out.count;
        }
        if (out.count == 0 || out.firstByteIdx < 0)
            return false;
        out.firstByte = out.bytes[out.firstByteIdx];
        return true;
    }

    [[nodiscard]] inline size_t ModuleSize(HMODULE mod) {
        const auto base = reinterpret_cast<const uint8_t*>(mod);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        return nt->OptionalHeader.SizeOfImage;
    }

    // Скан: первый non-wildcard байт как якорь (линейный, без SIMD;
    // вызывается один раз за 5 секунд только для НЕНАЙДЕННЫХ паттернов).
    [[nodiscard]] inline uintptr_t ScanPattern(const ParsedPattern& pat) {
        const HMODULE mod = GetModuleHandleA(pat.module);
        if (!mod)
            return 0;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
        const size_t size = ModuleSize(mod);
        if (!base || size < 32)
            return 0;
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(base);
        const uint8_t* end = begin + size;
        const uint8_t first = pat.firstByte;

        for (const uint8_t* p = begin; p + pat.count <= end; ++p) {
            if (*p != first)
                continue;
            bool ok = true;
            for (int j = 0; j < pat.count; ++j) {
                if (pat.wildcard[j])
                    continue;
                if (p[j] != pat.bytes[j]) { ok = false; break; }
            }
            if (!ok)
                continue;

            uintptr_t result = reinterpret_cast<uintptr_t>(p);
            if (pat.opRelCall) {
                const uintptr_t operand = reinterpret_cast<uintptr_t>(p) + pat.opByteOffset + 1;
                if (operand + 4 > base + size)
                    continue;
                const int32_t rel = *reinterpret_cast<const int32_t*>(operand);
                result = operand + 4 + rel;
            } else if (pat.opRipLoad) {
                const uintptr_t operand = reinterpret_cast<uintptr_t>(p) + pat.opByteOffset;
                if (operand + 4 > base + size)
                    continue;
                const int32_t rel = *reinterpret_cast<const int32_t*>(operand);
                result = operand + 4 + rel;
            }
            result += static_cast<uintptr_t>(pat.postOffset);
            if (pat.derefFinal) {
                if (result + 8 > static_cast<uintptr_t>(-1) &&
                    !mem::IsValidPtr(reinterpret_cast<const void*>(result), 8))
                    continue;
                result = *reinterpret_cast<const uintptr_t*>(result);
            }
            return result;
        }
        return 0;
    }

    // --- Паттерн-таблица (только то, что нужно) --------------------------
    struct Table {
        uintptr_t csgoInput = 0;             // объект CCSGOInput (не разыменовывать)
        uintptr_t entityList = 0;            // глобал CGameEntitySystem
        uintptr_t gameEntitySystem = 0;      // система (корень для layout-поиска)
        uintptr_t localPlayerController = 0;
        uintptr_t globalVars = 0;
        uintptr_t viewMatrix = 0;
        uintptr_t createMove = 0;            // функция CreateMove напрямую
        uintptr_t handleViewAngles = 0;      // функция HandleViewAngles
        uintptr_t getUserCmd = 0;            // функция GetUserCmd (velocity-сигнатура)
        uintptr_t gameTraceManager = 0;
        uintptr_t traceRay = 0;
        uintptr_t traceRayEntity = 0;
        uintptr_t traceFilterInit = 0;
        uintptr_t traceFilterSetCollision = 0;
        uintptr_t traceHull = 0;
        uintptr_t computeRandomSeed = 0;     // get_tick_view_angles (14178-пролог)
        uintptr_t weaponCalculateSpread = 0;
        uintptr_t baseFireGunsGetInaccuracy = 0;
        uintptr_t getInaccuracy = 0;
        uintptr_t getSpread = 0;
        uintptr_t predictionSeed = 0;
        uintptr_t predictionState = 0;
        int found = 0;
    };

    struct Entry { const char* name; const char* pattern; uintptr_t Table::* ptr; };

    inline constexpr Entry kTable[] = {
        { "csgo_input",               "client.dll:84C0740C488D0D*????????E8????????",   &Table::csgoInput },
        { "entity_list",              "client.dll:488B0D*????????8BFBC1EB0E",          &Table::entityList },
        { "game_entity_system",       "client.dll:488B0D*????????EB028BC6",            &Table::gameEntitySystem },
        { "local_player_controller",  "client.dll:48391D*????????7504B001",            &Table::localPlayerController },
        { "global_vars",              "client.dll:488B05*????????448B4044",            &Table::globalVars },
        { "view_matrix",              "client.dll:488D0D*????????48C1E006",            &Table::viewMatrix },
        { "create_move",              "client.dll:FFFFFFFF488D05*????????48890D????????+28~", &Table::createMove },
        { "handle_view_angles",       "client.dll:FFFFFFFF488D05*????????48890D????????+40~", &Table::handleViewAngles },
        { "get_usercmd",              "client.dll:40534883EC208BDAE8????????4C8BC0",   &Table::getUserCmd },
        { "game_trace_manager",       "client.dll:488B0D*????????488D3452~",           &Table::gameTraceManager },
        { "trace_ray",                "client.dll:48895424??48894C24??5553565741564157488DAC24????????B8E8240000", &Table::traceRay },
        { "trace_ray_entity",         "client.dll:44246848897C2420>E8????????488B3D????????", &Table::traceRayEntity },
        { "trace_filter_init",        "client.dll:48895C24??48897424??574883EC??0FB641??33FF24", &Table::traceFilterInit },
        { "trace_filter_set_collision", "client.dll:000041B800010000>E8????????4C397F38", &Table::traceFilterSetCollision },
        { "trace_hull",               "client.dll:>E8????????0F2F754C",                &Table::traceHull },
        { "compute_random_seed",      "client.dll:48895C2408574881ECF0000000F30F100A488D8C2410010000418BD8488BFAE8????????F30F104F04", &Table::computeRandomSeed },
        { "weapon_calculate_spread",  "client.dll:28F3440F11442420>E8????????488D85B0000000", &Table::weaponCalculateSpread },
        { "base_fire_guns_get_inaccuracy", "client.dll:>E8????????84C00F84C6FEFFFF",   &Table::baseFireGunsGetInaccuracy },
        { "get_inaccuracy",           "client.dll:48895C24??5556574881EC????????440F298424", &Table::getInaccuracy },
        { "get_spread",               "client.dll:4883EC??486391",                     &Table::getSpread },
        { "prediction_seed",          "client.dll:8B3D*????????488B03488BCB",         &Table::predictionSeed },
        { "prediction_state",         "client.dll:488B0D*????????33D28B5B38",         &Table::predictionState },
    };

    // --- Обновление: сканируем только недостающие (раз в 5 с) -------------
    inline Table& Globals() { static Table g; return g; }

    inline void Update() {
        static DWORD lastScan = 0;
        const DWORD now = GetTickCount();
        if (lastScan && now - lastScan < 5000)
            return;
        lastScan = now;

        for (const Entry& e : kTable) {
            uintptr_t& slot = Globals().*(e.ptr);
            if (slot)
                continue;
            ParsedPattern pat{};
            if (!ParsePattern(e.pattern, pat)) {
                NW_LOG(L"velo: паттерн '%s' не распарсен.", e.name);
                continue;
            }
            slot = ScanPattern(pat);
            if (slot) {
                ++Globals().found;
                NW_LOG(L"velo: %s = 0x%llX (pattern ok).", e.name,
                       static_cast<unsigned long long>(slot));
            }
        }
        static bool summaryLogged = false;
        if (!summaryLogged) {
            summaryLogged = true;
            NW_LOG(L"velo: таблица паттернов: найдено %d из %zu.",
                   Globals().found, sizeof(kTable) / sizeof(kTable[0]));
        }
    }

    // --- Cvar-ридер (VEngineCvar007, FNV-хэш, значение в +0x58) -----------
    struct CvarEntry { void* cvar; uint16_t prev; uint16_t next; };
    struct EngineCvar { uint8_t pad[0x50]; CvarEntry* entries; };
    struct Convar {
        const char* name;       // 0x00
        const void* defaultPtr; // 0x08
        const void* min;        // 0x10
        const void* max;        // 0x18
        const char* desc;       // 0x20
        uint16_t type;          // 0x28
        uint32_t changeCount;   // 0x2C
        uint64_t flags;         // 0x30
        uint8_t pad[0x20];      // 0x38..0x57
        union {                 // 0x58 (union float/int/string, как в velocity)
            float fl;
            int i32;
            uint8_t i1;
            const char* str;
        } value;
    };

    [[nodiscard]] inline const Convar* FindCvar(const char* name) {
        // Кэш: cvar-объекты живут всё время матча, обход таблицы (тысячи
        // VirtualQuery) на каждый тик — недопустимо. Негативный кэш — 5 с.
        // Ключ — указатель на литерал (вызывающие передают константные
        // строки; одинаковый литерал = одинаковый адрес).
        struct C { const char* name = nullptr; const Convar* var = nullptr; DWORD at = 0; };
        static C cache[8];
        C* slot = nullptr;
        for (auto& c : cache) {
            if (c.name == name) { slot = &c; break; }
            if (!slot && c.name == nullptr) slot = &c;
        }
        if (!slot) slot = &cache[0];
        if (slot->name != nullptr) {
            if (slot->var)
                return slot->var;
            if (GetTickCount() - slot->at < 5000)
                return nullptr;
        }
        slot->name = name;
        slot->at = GetTickCount();
        slot->var = nullptr;

        const HMODULE mod = GetModuleHandleW(L"tier0.dll");
        if (!mod) return nullptr;
        using CI = void*(__fastcall*)(const char*, int*);
        auto ci = reinterpret_cast<CI>(GetProcAddress(mod, "CreateInterface"));
        if (!ci) return nullptr;
        const auto* sys = reinterpret_cast<const EngineCvar*>(ci("VEngineCvar007", nullptr));
        if (!sys || !sys->entries) return nullptr;

        const uint32_t target = fnv1a::hash(name, std::strlen(name));
        uint16_t index = 0;
        for (int n = 0; n < 65535; ++n) {
            const CvarEntry entry = sys->entries[index];
            if (!entry.cvar)
                break;
            const auto* var = reinterpret_cast<const Convar*>(entry.cvar);
            const char* nm = var->name;
            if (nm && mem::IsValidPtr(reinterpret_cast<const void*>(nm), 64) &&
                fnv1a::runtime_hash(nm) == target) {
                slot->var = var;
                return var;
            }
            if (entry.next == 0xFFFF || entry.next == index)
                break;
            index = entry.next;
        }
        return nullptr;
    }

    [[nodiscard]] inline float CvarFloat(const char* name, float fallback) {
        const auto* v = FindCvar(name);
        return v ? v->value.fl : fallback;
    }

    [[nodiscard]] inline bool CvarBool(const char* name, bool fallback) {
        const auto* v = FindCvar(name);
        return v ? (v->value.i1 != 0) : fallback;
    }

    [[nodiscard]] inline int CvarInt(const char* name, int fallback) {
        const auto* v = FindCvar(name);
        return v ? v->value.i32 : fallback;
    }
}
