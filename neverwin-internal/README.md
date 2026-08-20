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
dwEntityList=0x2554050
dwLocalPlayerPawn=0x23A9118
dwViewAngles=0x23BF1A8
m_iHealth=0x34C
m_iTeamNum=0x3E7
m_fFlags=0x3F4
m_aimPunchAngle=0x14CC
m_pClippingWeapon=0x1308
m_iClip1=0x15A4
m_bInReload=0x1704
m_pGameSceneNode=0x318
m_vecAbsOrigin=0xC8
m_pCameraServices=0x1150
m_vecViewOffset=0x10D8
```

(значения в hex; `listEntryOffset`/`entryStride` тоже можно переопределить,
но они почти никогда не меняются)

## Управление

| Клавиша | Действие |
|---|---|
| INSERT | открыть/закрыть меню |
| F1 | реверс аимбот (наводка на ближайшего живого тиммейта) |
| F2 | антиаимлесс (взгляд в пол при враге) |
| F3 | visual recoil x4 |
| F4 | антибхоп |
| F5 | gamesense (дроп оружия) |
| END | выгрузка DLL |

Меню рендерится в DX11-цепочке игры: на стандартном рендере CS2 всё видно.
На `-vulkan` меню не появится (хук DXGI там не срабатывает), фичи при этом работают.

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
3. В игре: INSERT — меню, END — выгрузка.

## Если опять вылетит

1. Открой `%TEMP%\neverwin.log` — там будет последняя строка перед крешем.
2. Сгенерируй свежий `neverwin.ini` из дампа (раздел выше) — стухшие оффсеты причина №1.
3. Убедись: DLL собрана **Release x64**, инжектор запущен от администратора.

## Структура

```
neverwin-internal/
├── src/
│   ├── main.cpp        — DllMain, главный поток, загрузка neverwin.ini, выгрузка
│   ├── features.cpp    — все фичи (логика из internal.txt, безопасно)
│   ├── gui.cpp         — DX11 Present-хук, WndProc-хук, ImGui-меню
│   ├── offsets.hpp     — встроенные оффсеты (дефолты)
│   ├── offsets.cpp     — чтение оффсетов из neverwin.ini
│   ├── memory.hpp      — безопасные Read/Write
│   ├── log.hpp         — лог в %TEMP%\neverwin.log
│   ├── util.hpp        — UTF-16 → UTF-8
│   └── pch.h           — общие инклуды
├── injector/
│   └── injector.cpp    — x64-инжектор с понятными ошибками
├── tools/
│   └── dump_to_ini.py  — генератор neverwin.ini из дампа cs2-dumper
├── thirdparty/imgui/   — ImGui 1.93 (распакован из imgui-master.zip в корне репо)
├── CMakeLists.txt
└── build.bat
```
