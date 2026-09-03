#pragma once
#include <atomic>

namespace gui {

    // P / INSERT — показать/скрыть меню (тогглит WndProc-хук DLL).
    extern std::atomic<bool> g_menuOpen;

    // Выгрузка: END или кнопка в меню. Снимается на рендер-потоке.
    extern std::atomic<bool> g_unloadRequested;

    // 1 = рендер-хук встал в игру, меню рисует сама DLL (quint-схема).
    // 0 = не встал (Vulkan / не нашли свопчейн) — меню и ESP недоступны
    // (оверлея в проекте больше нет), фичи работают.
    extern std::atomic<bool> g_inGameMenuReady;

    // Инициализация меню: свопчейн игры, MinHook на Present/ResizeBuffers,
    // InputSystem::IsRelativeMouseMode (освобождение мыши), WndProc-хук.
    // Возвращает false, если свопчейн не найден (фичи работают, меню — оверлей).
    bool Init();

    // Ждёт, пока рендер-поток снимет хуки, и выгружает DLL.
    // Не возвращает управление (FreeLibraryAndExitThread).
    [[noreturn]] void ShutdownAndExit(HMODULE hModule);
}
