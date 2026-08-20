# NEVERWIN — release

Готовые сборки: DLL + инжектор. Правило версий простое: **чем больше число в
приписке `vN`, тем новее сборка**. Первая сборка — `v1`, следующая — `v2`,
и так далее. Старые версии не удаляются, лежат рядом для истории.

## Что лежит

| Файл | Что это |
|---|---|
| `neverwin_vN.dll` | внутренняя DLL (x64 Release, статический рантайм) |
| `neverwin_injector_vN.exe` | инжектор |
| `neverwin_overlay_vN.exe` | внешний HUD-оверлей (D2D-окно поверх игры, не инжектится) |
| `neverwin.ini` | оффсеты от последнего дампа (если сгенерирован) — DLL читает его при инжекте |

## Бинды

INSERT — ImGui-меню · F1 реверс аимбот · F2 антиаимлесс · F3 visual recoil ·
F4 антибхоп · F5 gamesense · F6 скрыть/показать HUD · END — выгрузка.

## Если меню не видно

1. Запусти `neverwin_overlay_vN.exe` — внешний HUD работает при любом рендерере
   (DX11 и Vulkan), состояние тянет из shared memory. Игра должна быть в
   оконном/borderless-режиме: поверх exclusive fullscreen оверлей не встанет.
2. Если играешь на DX11 и ImGui-меню нет — смотри `%TEMP%\neverwin.log`:
   строки про «хук Present» скажут, что не встало. С v3 хук цепляется за обе
   vtable (IDXGISwapChain + IDXGISwapChain1).

`neverwin.ini` общий для всех версий в этой папке: DLL ищет его рядом с собой.
Нет ini — DLL работает на встроенных оффсетах (и почти наверняка они стухшие,
если CS2 обновилась).

## Обновить оффсеты после патча Valve

1. Сними дамп: cs2-dumper с запущенной CS2 → папка `output/`.
2. Сгенерируй ini:
   ```
   python neverwin-internal/tools/dump_to_ini.py "C:\путь\к\output" "C:\папка с DLL\neverwin.ini"
   ```
3. Положи `neverwin.ini` в эту папку (рядом с DLL). Пересобирать DLL не нужно.
   Хочешь обновить и встроенные оффсеты в коде — `neverwin-internal/src/offsets.hpp`.

## Сборка релиза

- **Windows (VS2022):** `release\build_release.bat` — или `build_release.bat 3` для явной версии 3.
- **Linux (кросс, zig):** `bash release/build_release.sh` — версия автоинкрементится.

Обе сборки кладут результат сюда с правильной припиской `vN`.
