#pragma once
#include <atomic>
#include <cstdint>

// Флаги фич. std::atomic — их одновременно читает/пишет и поток фич,
// и поток рендера (ImGui-меню), гонки быть не должно.
struct Features {
    // F1 — реверс аим: 0=выкл, 1=raimv1 (прямая запись viewAngles),
    // 2=raimv2 (запись в юзеркоманду + фолбэк на прямую запись).
    std::atomic<int> reverseAim{0};
    std::atomic<bool> antiAimless{false};  // F2 — взгляд в пол при видимом враге
    std::atomic<float> spinSpeed{1.0f};    // скорость спинбота F2: 0..10 (множитель)
    std::atomic<bool> visualRecoil{false}; // F3 — отдача x4
    std::atomic<bool> antiBhop{false};     // F4 — сброс FL_ONGROUND при прыжке
    std::atomic<bool> gamesense{true};     // F5 — рандомный дроп оружия (20%)

    // --- Вкладка VELOCITY: аимбот по духу velocity. ---
    // Цель: ближайший по FOV живой павн выбранной стороны, через стены.
    std::atomic<bool> rageAim{false};    // включён (при включении raim отключается)
    std::atomic<int>  rageTarget{0};     // 0 = тиммейты (team), 1 = враги (nonteam)
    std::atomic<float> rageFov{180.0f};  // окно FOV в градусах (180 = всё)
    std::atomic<float> rageSmooth{1.0f}; // сглаживание 1..30 (1 = мгновенно)
};

// Снапшот состояния для отображения в меню (диагностика).
struct DebugState {
    std::atomic<uintptr_t> clientBase{0};
    std::atomic<uintptr_t> entityList{0};
    std::atomic<uintptr_t> localPlayer{0};
    std::atomic<int>       localHealth{0};
    std::atomic<int>       localTeam{0};
    std::atomic<bool>      viewAnglesWritable{false};
    std::atomic<bool>      offsetsFromIni{false}; // true = оффсеты из neverwin.ini
};

extern Features   g_features;
extern DebugState g_state;

// Главный цикл фич. Крутится, пока не запрошена выгрузка
// (gui::g_unloadRequested), после чего возвращает управление.
void RunFeatureLoop();
