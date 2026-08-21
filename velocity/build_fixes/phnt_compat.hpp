#pragma once
// ============================================================================
// Совместимость vendored-phnt <-> mingw/clang при кросс-сборке velocity.
// В вырезанной копии phnt нет определения EXTERN_C_START/END (в оригинале
// они в phnt_windows.h), а C_ASSERT из ntintsafe.h конфликтует с crt mingw.
// ============================================================================
#ifdef __cplusplus
#define EXTERN_C_START extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_START
#define EXTERN_C_END
#endif

// Пустая ветка C_ASSERT в ntintsafe.h — статические проверки размеров
// не нужны, а повторное определение typedef-массива роняет сборку.
#define SORTPP_PASS

// UFIELD_OFFSET — макрос MSVC (winnt.h), в mingw его нет.
#ifndef UFIELD_OFFSET
#define UFIELD_OFFSET(type, field) __builtin_offsetof(type, field)
#endif

// В вырезанной копии phnt нет ntlsa.h, а ntioapi.h ссылается на
// STORAGE_RESERVE_ID без определения (обрезано). velocity их не использует —
// хватает пустых деклараций. (форсед-инклуд идёт ДО windows.h — тянем сами)
#include <windows.h>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
typedef struct _STORAGE_RESERVE_ID {
    ULONG StorageReserveId;
} STORAGE_RESERVE_ID, *PSTORAGE_RESERVE_ID;

// В mingw winnt.h нет RTL_SYSTEM_GLOBAL_DATA_ID (он появляется только при
// NTDDI >= WIN10_FE), а в вырезанной копии phnt его typedef под тем же гвардом.
#ifndef RTL_SYSTEM_GLOBAL_DATA_ID
typedef ULONG RTL_SYSTEM_GLOBAL_DATA_ID;
#endif
