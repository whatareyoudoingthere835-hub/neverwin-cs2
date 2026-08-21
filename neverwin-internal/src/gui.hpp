#pragma once
#include <atomic>

namespace gui {

    // P — показать/скрыть меню. Меню теперь ВНЕШНЕЕ: рендерится оверлеем
    // neverwin_overlay_vN.exe (DirectX 12), DLL только выставляет флаг в
    // shared memory. Внутриигрового хука рендера больше нет — именно он
    // ронял игру при инжекте, когда ImGui рисовал в flip-model свопчейн CS2.
    extern std::atomic<bool> g_menuOpen;

    // Выгрузка: END или кнопка в меню оверлея (команда из shared memory).
    extern std::atomic<bool> g_unloadRequested;

    // F6 — показать/скрыть HUD-оверлей (читает сам оверлей из shared memory).
    extern std::atomic<bool> g_hudVisible;

    // Хуков больше не ставим — всегда true. Оставлено для совместимости
    // с main.cpp (там ветка предупреждения на случай отказа).
    bool Init();

    // Освобождает DLL. Не возвращает управление (FreeLibraryAndExitThread).
    [[noreturn]] void ShutdownAndExit(HMODULE hModule);
}
