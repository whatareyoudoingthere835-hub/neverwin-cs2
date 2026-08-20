#pragma once
#include <atomic>

namespace gui {

    // INSERT — показать/скрыть меню (пишет поток фич, читает поток рендера).
    extern std::atomic<bool> g_menuOpen;

    // Выгрузка: поток фич выставляет флаг, поток рендера на ближайшем Present
    // снимает хуки и сигналит событие, после чего DLL можно освобождать.
    extern std::atomic<bool> g_unloadRequested;

    // Ставит хук IDXGISwapChain::Present через dummy-устройство.
    // Возвращает false, если D3D11 недоступен (фичи продолжат работать без меню).
    bool Init();

    // Ждёт, пока рендер-поток снимет хуки, и выгружает DLL.
    // Не возвращает управление (FreeLibraryAndExitThread).
    [[noreturn]] void ShutdownAndExit(HMODULE hModule);
}
