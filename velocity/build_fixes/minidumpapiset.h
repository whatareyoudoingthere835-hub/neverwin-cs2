#pragma once
// ============================================================================
// Шим для кросс-сборки velocity под mingw/clang:
// в mingw-w64 нет minidumpapiset.h (заголовок Windows SDK), а phnt_windows.h
// включает его безусловно. Сам код velocity MiniDump* не использует —
// объявляем только типы и один импорт, чтобы скомпилировалось.
// Импорт линкуется через dbghelp.def из комплекта mingw (-ldbghelp).
// ============================================================================
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef DWORD MINIDUMP_TYPE;

enum {
    MiniDumpNormal = 0,
    MiniDumpWithDataSegs = 1,
    MiniDumpWithFullMemory = 2,
    MiniDumpWithHandleData = 4,
    MiniDumpFilterMemory = 8,
    MiniDumpScanMemory = 0x10,
    MiniDumpWithUnloadedModules = 0x20,
    MiniDumpWithIndirectlyReferencedMemory = 0x40,
    MiniDumpFilterModulePaths = 0x80,
    MiniDumpWithProcessThreadData = 0x100,
    MiniDumpWithPrivateReadWriteMemory = 0x200,
    MiniDumpWithoutOptionalData = 0x400,
    MiniDumpWithFullMemoryInfo = 0x800,
    MiniDumpWithThreadInfo = 0x1000,
    MiniDumpWithCodeSegs = 0x2000,
    MiniDumpWithoutAuxiliaryState = 0x4000,
    MiniDumpWithFullAuxiliaryState = 0x8000,
    MiniDumpWithPrivateWriteCopyMemory = 0x10000,
    MiniDumpIgnoreInaccessibleMemory = 0x20000,
    MiniDumpWithTokenInformation = 0x40000,
};

typedef struct _MINIDUMP_EXCEPTION_INFORMATION {
    DWORD ThreadId;
    PVOID ExceptionPointers;
    BOOL  ClientPointers;
} MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;

typedef struct _MINIDUMP_USER_STREAM_INFORMATION {
    ULONG UserStreamCount;
    PVOID UserStreamArray;
} MINIDUMP_USER_STREAM_INFORMATION, *PMINIDUMP_USER_STREAM_INFORMATION;

typedef struct _MINIDUMP_CALLBACK_INFORMATION {
    BOOL  CallbackRoutine;
    PVOID CallbackParam;
} MINIDUMP_CALLBACK_INFORMATION, *PMINIDUMP_CALLBACK_INFORMATION;

WINBASEAPI BOOL WINAPI MiniDumpWriteDump(
    HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

#ifdef __cplusplus
}
#endif
