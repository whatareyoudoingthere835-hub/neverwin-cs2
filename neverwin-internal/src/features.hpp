#pragma once
#include <atomic>
#include <cstdint>

// Флаги фич. std::atomic — их одновременно читает/пишет и поток фич,
// и поток рендера (ImGui-меню), гонки быть не должно.
struct Features {
    // F1 — реверс аим: 0=выкл, 1=raimv1 (прямая запись viewAngles),
    // 2=raimv2 (запись в юзеркоманду + фолбэк на прямую запись).
    // F1 только включает/выключает выбранный в меню режим.
    std::atomic<bool> reverseAimEnabled{false};
    std::atomic<int> reverseAimMode{1}; // 1=raimv1, 2=raimv2, 3=test
    // Поворот F1 задаётся во времени, а не длиной движения мыши.
    // speed — максимум градусов в секунду, smooth — время сглаживания в мс.
    std::atomic<float> reverseAimSpeed{720.0f};
    std::atomic<float> reverseAimSmooth{0.0f};
    std::atomic<float> reverseAimPrediction{0.12f}; // секунд вперёд по velocity цели
    std::atomic<bool> antiAimless{false};  // F2 — взгляд в пол при видимом враге
    std::atomic<float> spinSpeed{1.0f};    // скорость спинбота F2: 0..10 (множитель)
    std::atomic<bool> visualRecoil{false}; // F3 — отдача x4
    std::atomic<bool> bhop{false};         // F4 — обычный auto-bhop при удержании SPACE
    std::atomic<bool> gamesense{true};     // F5 — рандомный дроп оружия (20%)
    // Nonagon ragebot (F6)
    std::atomic<bool> ragebot{false};      // F6 — рейджбот с резолвером из nonagon
    std::atomic<bool> rageAutoFire{true};  // автоогонь/триггер после подтверждения живой цели
    std::atomic<bool> resolver{true};      // резолвер вкл/выкл
    std::atomic<bool> backtrack{true};     // бэктрек
    std::atomic<int>  rageFov{180};        // фов рейджа
    std::atomic<int>  rageHitchance{50};
    std::atomic<int>  rageMinDamage{1};
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
