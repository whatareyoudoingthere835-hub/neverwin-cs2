#pragma once
#include <atomic>

namespace gui {

    // INSERT — показать/скрыть меню (пишет поток фич, читает поток рендера).
    extern std::atomic<bool> g_menuOpen;

    // Выгрузка: поток фич выставляет флаг, поток рендера на ближайшем Present
    // снимает хуки и сигналит событие, после чего DLL можно освобождать.
    extern std::atomic<bool> g_unloadRequested;

    // F6 — показать/скрыть внешний HUD-оверлей (читает сам оверлей из shared memory).
    extern std::atomic<bool> g_hudVisible;

    // Ставит хуки Present на IDXGISwapChain И IDXGISwapChain1 через
    // dummy-устройство. CS2 презентит кадр через IDXGISwapChain1 — без
    // второго хука меню на DX11 не появлялось вообще.
    // Возвращает false, если D3D11 недоступен (фичи продолжат работать без меню).
    bool Init();

    // Ждёт, пока рендер-поток снимет хуки, и выгружает DLL.
    // Не возвращает управление (FreeLibraryAndExitThread).
    [[noreturn]] void ShutdownAndExit(HMODULE hModule);
}
