// NEVERWIN — внутренний DLL для CS2 (x64).
// Входная точка: DllMain -> HackThreadStub -> RunFeatureLoop -> выгрузка.
#include "pch.h"
#include "features.hpp"
#include "gui.hpp"
#include "log.hpp"

namespace {

    // Отдельная функция вместо лямбды: у лямбды конвенция __cdecl,
    // а CreateThread ждёт __stdcall — на x86 это дало бы испорченный стек.
    // На x64 конвенция одна, но пусть будет правильно.
    DWORD WINAPI HackThreadStub(LPVOID param) {
        const HMODULE hModule = static_cast<HMODULE>(param);

        nwlog::Init();
        NW_LOG(L"DLL загружена. Поток %lu, PID %lu.",
               GetCurrentThreadId(), GetCurrentProcessId());

        if (!gui::Init()) {
            NW_LOG(L"WARNING: хук Present не встал — работаем без меню.");
        }

        // Крутится до VK_END / кнопки "Выгрузить DLL".
        RunFeatureLoop();

        // Анхук + FreeLibraryAndExitThread. Не возвращается.
        gui::ShutdownAndExit(hModule);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // Наш поток не вызывает LoadLibrary — риска loader-lock дедлока нет.
        DisableThreadLibraryCalls(hModule);
        const HANDLE hThread = CreateThread(nullptr, 0, HackThreadStub, hModule, 0, nullptr);
        if (hThread)
            CloseHandle(hThread); // хэндл потоку больше не нужен
    }
    return TRUE;
}
