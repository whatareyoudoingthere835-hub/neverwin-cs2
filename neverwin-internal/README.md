# NEVERWIN — внутренний DLL (фикс + ImGui-меню)

Исправленная версия `internal.txt` из корня репозитория + меню на ImGui (DX11) + инжектор.
Всё, что сделано — в этой папке; `internal.txt` не тронут (эталонный прототип),
`besthack.py` (Python Edition) держится в синхроне по фичам.

**Готовые сборки** — в [`../release`](../release): `neverwin_vN.dll` + `neverwin_injector_vN.exe`,
чем больше N — тем новее. Сборка релиза: `release/build_release.bat` (Windows) или
`release/build_release.sh` (Linux, кросс через zig).

---

## Почему вылетало при инжекте (разбор internal.txt)

**1. Сырые чтения по указателям без единой проверки.**
`READ_MEM/WRITE_MEM` = `*(type*)(addr)`. Любое битое значение адреса — мгновенный
access violation и креш игры. Оффсеты в файле от конкретного дампа (0x2554050 и т.д.),
а CS2 обновляется почти каждую неделю — уже через один апдейт часть адресов ведёт
в мусор. → Каждый доступ теперь идёт через `mem::Read/mem::Write` с проверкой
`VirtualQuery` (`src/memory.hpp`), битые адреса просто пропускаются.

**2. Ошибка в формуле энтити-листа в цикле врагов.**
В ветке оружия формула правильная: `list + 0x10 + 8*(idx>>9)`. В цикле врагов —
`((8*(i & 0x7FFF)) >> 9) + 16`, что равно `16 + (i>>6)`. При `(i & 0x1FF) >= 64`
читается чужой `listEntry` → мусорный pawn → креш при включённом антиаимботе.
→ Вынесено в одну функцию `GetEntityByHandle()` с одной правильной формулой.

**3. Запись во viewAngles без VirtualProtect.**
Если секция read-only (а у свежих билдов так и есть) — запись = креш.
→ `mem::Write` сам делает регион writable.

**4. Никакой защиты от битых данных.**
Не было ни SEH, ни проверок указателей — первая же битая единица данных роняла
процесс целиком. → Теперь нулевые/битые значения пропускаются, цикл живёт дальше.

**5. Debug-сборка и разрядность.**
DLL из Debug тянет `vcruntime140d.dll`/`msvcp140d.dll`, которых нет на игровой машине —
`LoadLibrary` молча падает, выглядит как «вылет при инжекте». Плюс x86-DLL в x64-игру
даёт `ERROR_BAD_EXE_FORMAT`. → CMake собирает только x64, Release, со статическим
рантаймом (`/MT`).

**6. CreateThread внутри DllMain.**
Классический риск loader-lock дедлока. В нашем случае поток не зовёт `LoadLibrary`,
так что это не было причиной, но паттерн оставлен аккуратным (см. `main.cpp`).

**7. Полное молчание.**
Ни одного лога — креш лечился вслепую. → Всё пишется в `%TEMP%\neverwin.log`
и в OutputDebugString (видно в DebugView). Теперь видно, где именно падает.

**8. `#include "pch.h"` без самого pch.**
Файла в проекте не было. → `pch.h` теперь есть (обычный заголовок, без магии
прекомпиляции — проект собирается и с ним, и без него).

**9. Вечное ожидание client.dll.**
Если DLL инжектнули не в CS2 — исходник молча висел в бесконечном цикле.
→ Ждём максимум 120 секунд, потом честный лог и выгрузка.

---

## Что добавлено

- **ImGui-меню** (хук `IDXGISwapChain::Present` через dummy-device + WndProc-хук для ввода).
  Чекбоксы всех фич, живая диагностика (client.dll, LocalPlayer, EntityList), кнопка выгрузки.
- **Кириллица в меню** — грузится `segoeui.ttf` (встроенный шрифт ImGui русский не рисует).
- **Корректная выгрузка**: END или кнопка → снятие обоих хуков, потом `FreeLibraryAndExitThread`.
- **Инжектор** `neverwin_injector.exe` с человекочитаемыми ошибками на каждом шаге.
- **Логи** в `%TEMP%\neverwin.log`.
- **Оффсеты из `neverwin.ini`** — после патча Valve ничего не пересобираешь,
  просто кладёшь свежий ini рядом с DLL (см. раздел ниже).

---

## Вольво обновили все — что делать

Оффсеты в CS2 меняются после каждого патча. Раньше это означало правку кода и
пересборку; теперь оффсеты живут в `neverwin.ini` рядом с DLL:

1. Запускаешь CS2 и [cs2-dumper](https://github.com/a2x/cs2-dumper) — он снимает
   свежие оффсеты и схемы в папку `output/`.
2. Генерируешь ini:
   ```
   python tools/dump_to_ini.py "C:\путь\к\cs2-dumper\output" "C:\папка с DLL\neverwin.ini"
   ```
3. Переинжект. Меню покажет зелёным **«Оффсеты: из neverwin.ini»** — значит свежие.

Если ini нет — DLL работает на встроенных значениях из `src/offsets.hpp`, и после
патча они почти наверняка стухшие: в меню `LocalPlayer: 0`, фичи молчат, в логе
предупреждение. Креша при этом не будет — все чтения памяти защищены.

Формат `neverwin.ini`:

```ini
[offsets]
dwEntityList=0x2555050
dwLocalPlayerPawn=0x23AA118
dwViewAngles=0x23C01A8
m_iHealth=0x34C
m_iTeamNum=0x3E7
m_fFlags=0x3F4
m_pGameSceneNode=0x330
m_vecViewOffset=0xE78
m_vecAbsOrigin=0xC8
m_pCameraServices=0x1240
m_pWeaponServices=0x1208
m_vecCsViewPunchAngle=0x48
m_hActiveWeapon=0x60
m_iClip1=0x1700
m_bInReload=0x1814
```

(значения в hex; `listEntryOffset`/`entryStride` тоже можно переопределить,
но они почти никогда не меняются)

## Управление

| Клавиша | Действие |
|---|---|
| P | открыть/закрыть меню оверлея |
| F1 | реверс аимбот (наводка на ближайшего живого тиммейта) |
| F2 | антиаимлесс (взгляд в пол при враге) |
| F3 | visual recoil x4 |
| F4 | антибхоп |
| F5 | gamesense (дроп оружия) |
| F6 | показать/скрыть HUD-оверлей |
| END | выгрузка DLL |

INSERT оставлен как запасной тоггл меню.

## Меню — в игре, по схеме quintcs2 (v5+)

Меню рисует сама DLL, в игровой бэкбуфер, хук стоит на **настоящем свопчейне
игры** — без единого dummy-устройства:
1. Свопчейн игры: сигнатура в `rendersystemdx11.dll` → глобал → слот →
   `c_swap_chain_dx_11*` → `+0x170` → `IDXGISwapChain*`.
2. MinHook (thirdparty/minhook, из исходников quintcs2) на `Present` (vtable 8)
   и `ResizeBuffers` (13).
3. Каждый кадр перебиндовываем свой RTV (`OMSetRenderTargets`) и рисуем ImGui
   прямо в бэкбуфер, затем оригинальный `Present`.
4. `InputSystem::IsRelativeMouseMode` (vtable 76, `InputSystemVersion001` из
   `inputsystem.dll`) — при открытом меню возвращает false: игра отдаёт
   курсор, иначе мышь в меню не работает.
5. WndProc-хук: тоггл меню, ввод в ImGui, глотание клавиш движения, чтобы
   игрок не бегал с открытым меню.

**P / INSERT** — открыть/закрыть меню. Чекбоксы включают фичи, кнопка
выгружает DLL.

## Канал углов F1/F2 — user cmd, как в quintcs2 (v6+)

Писать `dwViewAngles` из фонового потока бесполезно: CS2 каждый тик
перезаписывает их из юзеркоманды, и аимбот проигрывал эту гонку. Теперь
углы пишутся так же, как в quintcs2: хук `CCSGOInput::CreateMove`
(vtable 5, `dwCSGOInput` из дампа), после оригинала — углы в текущий
user cmd:

```
dwLocalPlayerController → контроллер → GetCmdManager (сигнатура quint)
→ cmd = manager + (sequence % 150) * 0x98
→ CUserCmdBasePB → m_view_angles (0x40) → QAngle + cached_bits |= 7
```

Раскладка протобуфа (`pb/base_cmd` смещения) решается рантайм-пробой:
варианты сверяются с текущими `dwViewAngles`, победитель логируется
(`раскладка user cmd: pb=0x.. base_cmd=0x..` в `%TEMP%\neverwin.log`).
Если канал недоступен (не нашлась глобалка/сигнатура) — фолбэк на прямую
запись viewAngles, как в старых версиях. Все причины отказа логируются.

## Оверлей — запасной путь (Vulkan)

`neverwin_overlay_vN.exe` (DirectX 12) остаётся на случай, если рендер-хук
в игру не встал: Vulkan, не совпала сигнатура после патча Valve. Тогда DLL
пишет `inGameMenu=0` в shared memory, и меню берёт на себя оверлей
(`Local\neverwin_state_v5`), плюс он всегда показывает HUD со статусами.

- Игра в оконном/borderless-режиме — поверх exclusive fullscreen внешнее окно не встанет.
- F6 — скрыть/показать HUD оверлея. END — выгрузка DLL (оверлей сам закроется).

Если рендер-хук не встал на DX11 — `%TEMP%\neverwin.log` скажет, что именно:
свопчейн, MinHook, InputSystem или WndProc.

## Сборка

### Windows + Visual Studio 2022 (рекомендуется)

```
build.bat
```

или вручную:

```
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Результат: `build\Release\neverwin.dll` и `build\Release\neverwin_injector.exe`.

### MinGW-w64

```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Результат: `build\neverwin.dll` и `build\neverwin_injector.exe`.

> **Только Release.** Debug-сборка не инжектится на чужой машине (см. пункт 5 выше).

## Использование

1. Запусти CS2, дождись загрузки.
2. `neverwin_injector.exe` — сам найдёт `cs2.exe` и `neverwin.dll` рядом с собой.
   (или `neverwin_injector.exe <PID> <путь\к\neverwin.dll>`)
3. Запусти `neverwin_overlay_vN.exe` и инжектни DLL. В игре: P — меню, END — выгрузка.

## Если опять вылетит

1. Открой `%TEMP%\neverwin.log` — там будет последняя строка перед крешем.
2. Сгенерируй свежий `neverwin.ini` из дампа (раздел выше) — стухшие оффсеты причина №1.
3. Убедись: DLL собрана **Release x64**, инжектор запущен от администратора.

## Структура

```
neverwin-internal/
├── src/
│   ├── main.cpp         — DllMain, главный поток, загрузка neverwin.ini, выгрузка
│   ├── features.cpp     — все фичи + shared memory (снапшот и команды оверлея)
│   ├── gui.cpp          — только флаги меню/HUD/выгрузки (хуков больше нет)
│   ├── shared_state.hpp — протокол DLL <-> оверлей (named shared memory)
│   ├── offsets.hpp      — встроенные оффсеты (дефолты)
│   ├── offsets.cpp      — чтение оффсетов из neverwin.ini
│   ├── memory.hpp       — безопасные Read/Write
│   ├── log.hpp          — лог в %TEMP%\neverwin.log
│   ├── util.hpp         — UTF-16 → UTF-8
│   └── pch.h            — общие инклуды
├── overlay/
│   └── overlay.cpp      — внешнее меню/HUD на DirectX 12 (ImGui + dx12)
├── injector/
│   └── injector.cpp     — x64-инжектор с понятными ошибками
├── tools/
│   └── dump_to_ini.py   — генератор neverwin.ini из дампа cs2-dumper
├── thirdparty/imgui/    — ImGui 1.93 (распакован из imgui-master.zip в корне репо);
│                          imgui_demo.cpp — эталонное меню (все виджеты, вкладки,
│                          хоткеи) — смотри, как устроены меню вообще
├── CMakeLists.txt
└── build.bat
```
