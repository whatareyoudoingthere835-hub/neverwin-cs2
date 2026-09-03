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
    // Сколько раз в секунду аимбот обновляет углы (1..120), а не «на сколько
    // градусов» он шагает. Реальный интервал между обновлениями = 1000/rate.
    std::atomic<int> reverseAimRate{120};
    std::atomic<bool> reverseAimTrigger{false}; // триггербот в обычном аимботе
    std::atomic<bool> silentAim{false};         // углы только в usercmd, камеру не трогаем
    std::atomic<bool> noSpread{false};          // компенсация спреда (aim + rage)
    std::atomic<float> reverseAimPrediction{0.15f}; // секунд вперёд по velocity цели
    std::atomic<bool> antiAimless{false};  // F2 — silent «в пол + спин» при видимом враге
    // Скорость спина F2 — ГРАДУСЫ В СЕКУНДУ (насколько быстро крутить),
    // а не «на сколько градусов за шаг». Время берётся из реальных миллисекунд.
    std::atomic<float> spinSpeed{720.0f};  // 10..3600 deg/s
    // --- Silent-канал (общий для aim и antiaimless) ---
    // Цикл фич вычисляет угол и кладёт его сюда; хук CreateMove каждый тик
    // дописывает его в usercmd ПОСЛЕ того, как игра заполнила команду, но
    // до отправки по сети. Это и есть silent: камера (dwViewAngles) стоит,
    // а отправленная команда несёт целевой угол. Писать в usercmd из цикла
    // фич бесполезно — следующий CreateMove затирает углы свежей камерой.
    std::atomic<float> silentPitch{0.0f};
    std::atomic<float> silentYaw{0.0f};
    std::atomic<bool>  silentValid{false}; // есть ли свежий угол на этот тик
    std::atomic<bool> visualRecoil{false}; // F3 — отдача x4
    std::atomic<bool> bhop{false};         // F4 — обычный auto-bhop при удержании SPACE
    std::atomic<bool> extHope{false};      // ExtHope — спам кликов SPACE пока зажат X
    std::atomic<int>  extHopeRate{32};    // прыжков (кликов) в секунду, 1..128
    std::atomic<bool> gamesense{true};     // F5 — рандомный дроп оружия (20%)
    std::atomic<bool> clanTag{false};      // animated [NeverWin] + base name
    std::atomic<bool> espEnabled{false};   // box ESP через view matrix
    std::atomic<bool> espHealth{true};     // полоска HP рядом с боксом
    std::atomic<bool> espDistance{true};   // дистанция в метрах под боксом
    // Дальность ESP в юнитах (1 м = 52.49). Бывший хардкод 3000 (~57 м)
    // — половина карты; теперь слайдер 25..400 м.
    std::atomic<float> espMaxDistance{10000.0f};
    std::atomic<bool> espTeammates{false}; // рисовать и своих (по умолчанию враги)
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
    std::atomic<bool>      entityLayoutVerified{false}; // runtime entity layout подтверждён
    // Мировая позиция локального павна для ESP-дистанции (обновляет feature loop).
    std::atomic<float>     localOriginX{0.0f};
    std::atomic<float>     localOriginY{0.0f};
    std::atomic<float>     localOriginZ{0.0f};
};

extern Features   g_features;
extern DebugState g_state;

// Главный цикл фич. Крутится, пока не запрошена выгрузка
// (gui::g_unloadRequested), после чего возвращает управление.
void RunFeatureLoop();
