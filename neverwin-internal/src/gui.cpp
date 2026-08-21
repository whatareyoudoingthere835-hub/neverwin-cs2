#include "pch.h"
#include "gui.hpp"
#include "log.hpp"

namespace gui {

    std::atomic<bool> g_menuOpen{false};
    std::atomic<bool> g_unloadRequested{false};
    std::atomic<bool> g_hudVisible{true};

    bool Init() {
        // Внутриигрового рендера больше нет: меню рисует внешний оверлей
        // (DirectX 12), состояние уходит в shared memory. Хук Present удалён —
        // он был причиной краша при инжекте (ImGui рисовал в свопчейн CS2).
        NW_LOG(L"меню внешнее: запусти neverwin_overlay_vN.exe (открытие на P).");
        return true;
    }

    void ShutdownAndExit(HMODULE hModule) {
        NW_LOG(L"выгружаю DLL.");
        FreeLibraryAndExitThread(hModule, 0);
    }
}
