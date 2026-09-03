# NEVERWIN — база знаний проекта (handoff-документ)

> Документ: полная выжимка всего найденного/починенного за всю работу над читом.
> Дата актуализации: 2026-09-03. Последняя сборка: **v78** (build CS2 14178).
> Цель: любой новый агент/сессия продолжает работу с этого файла без повторного реверса.

---

## 1. Структура проекта

```
neverwin-internal/          — основной DLL-проект
  src/
    main.cpp                — DllMain → поток → оффсеты из ini → gui::Init → RunFeatureLoop
    features.cpp            — весь feature loop + CreateMove hook + probes (самый большой файл)
    features.hpp            — Features/DebugState (атомики, общий доступ GUI↔loop)
    entities.hpp            — entity list, PlayerSnapshot, dual-verify discovery, bone cache
    offsets.hpp/.cpp        — все оффсеты + загрузка neverwin.ini
    memory.hpp              — mem::Read/Write (VirtualQuery-защита) + ReadFast/WriteFast (hot path)
    gui.cpp                 — DX11 Present hook, ImGui, меню (MemeSense-style), ESP
    pb_cmd.hpp              — protobuf-слой usercmd по официальным PB-хедерам
    usercmd_probe.hpp       — pattern-сканер всех функций client.dll
    usercmd_apply.hpp       — V16 input.apply (кнопки, CRC, subtick)
    nospread.hpp            — компенсация спреда (seed → spread → углы)
    clantag.hpp             — анимированный [NeverWin] + базовый ник
    assets/                 — fa_solid.hpp (Font Awesome), icons_fontawesome.h, gui/esp_icons.hpp
  injector/injector.cpp     — x64 инжектор (CreateRemoteThread + LoadLibraryW)
  thirdparty/               — imgui 1.93, minhook
  tools/dump_to_ini.py      — cs2-dumper output → neverwin.ini
release/                    — neverwin_vN.dll / neverwin_injector_vN.exe (v78 актуальная)
velocity/, quint/           — исходники-доноры (паттерны, логика, SDK)
CHEAT.zip                   — сторонний исходник (SDK: CGameEntitySystem stride 0x70, bone cache)
velocity-fixed-main...zip   — V16-фикс velocity (input.apply!, пароль архива: hvh.net)
CS2-OFFSETS-main.zip        — расширенный дамп (exitscam-dumper), те же оффсеты
output.zip                  — дамп 14178 (в ветке, актуальный)
```

Сборка (Linux, zig): `ZIG=<путь>/zig bash ./release/build_release.sh N` — автоматически ставит версию N.
venv для zig: `python3 -m venv /tmp/neverwin-tools && pip install ziglang`,
бинарь: `/tmp/neverwin-tools/lib/python3.11/site-packages/ziglang/zig`.

---

## 2. Актуальные оффсеты (CS2 build 14178, дамп 2026-09-01)

### module (client.dll)
| Оффсет | Значение | Примечание |
|---|---|---|
| dwEntityList | 0x2571220 | |
| dwLocalPlayerPawn | 0x23C6268 | рабочая точка входа, живёт всегда |
| dwLocalPlayerController | 0x23A0F30 | |
| dwViewAngles | 0x23DC2F8 | прямая запись = видимое вращение камеры |
| dwCSGOInput | 0x23DBC70 | **СТАТИЧЕСКИЙ объект, НЕ разыменовывать** (см. §5) |
| dwViewMatrix | 0x23CB830 | ESP, 16 float |
| dwGlobalVars | 0x20AF5F0 | curtime +0x30, frametime +0x08 |

### schema (не менялись 14176→14178)
m_iHealth=0x34C, m_lifeState=0x354, m_iTeamNum=0x3E7, m_fFlags=0x3F4,
m_pGameSceneNode=0x330, m_vecViewOffset=0xE78, m_vecAbsOrigin=0xC8, m_bDormant=0x103,
m_pCameraServices=0x1240, m_pWeaponServices=0x1208, m_pMovementServices=0x1248,
m_hPlayerPawn=0x914 (CCSPlayerController), **m_hPawn=0x600** (CBasePlayerController, был 0x6BC до 14177!),
m_hController=0x13D0, m_bPawnIsAlive=0x91C, m_iIDEntIndex=0x342C,
m_iItemDefinitionIndex=0x1BA (uint16, в vdata!).
MovementServices: m_nLastCommandNumberProcessed=0x188, m_flCmdForwardMove=0x1A0, m_flCmdLeftMove=0x1A4, m_vecLastMovementImpulses=0x1CC.
Кости: bone array = sceneNode+0x150 → +0x80 → ptr; stride 0x20; **BONE_HEAD=7** (CBoneData{vec3,scale,quat}).
Высший индекс энтити: entitySystem+0x2090.

---

## 3. Entity list — как работает и как чинился

Формула: `chunk = root + listOffset + 8*(index>>9)`, `entity = chunk + stride*(index&0x1FF)`.
**Подтверждено на 14177/14178: listOffset=0x10, stride=0x70** (stride взят из CHEAT.zip `CGameEntitySystem.hpp`).

История багов:
1. 14176: работало +0x10/0x78 → после апдейта сломалось. Причина: runtime-layout не совпал, fallback читал мусор.
2. Открыли `DiscoverEntityListLayout` — поиск по известному local pawn. Один pawn оказался НЕдостаточно: поймали ложный `listOffset=0x0` (в первом поле системы лежали указатели, похожие на chunk).
3. Итог — **dual-verify** (`DiscoverEntityListLayoutVerified`): принимаем layout только если
   (а) local controller реально найден в слотах 1..64 chunk[0], и
   (б) handle, прочитанный с подтверждённого контроллера (m_hPawn ИЛИ m_hPlayerPawn), резолвится ровно в local pawn.
4. Retry каждые 5 сек (первый запуск ловит момент, когда controller ещё NULL при загрузке матча).
5. Chunk валидировать под конкретный stride (512*stride байт), НЕ жёстко 64KB — иначе 0x70-чанки отбрасываются.

Успех в логе: `entity-list: layout подтверждён (controller slot N): ... listOffset=0x10 stride=0x70`.
`g_state.entityLayoutVerified` — гейт для ESP-скана (без него скан не запускается).

Сканер игроков: только слоты 1..64 = CCSPlayerController → m_hPlayerPawn/m_hPawn → pawn.
Живость у remote-целей: m_bPawnIsAlive + HP>0 + lifeState==0 (для F1 ослаблено до HP+lifeState — m_bPawnIsAlive бывает отстающим).
F1 (reverse aim) целится в **тиммейтов** (спецпроект), ragebot — во врагов.

---

## 4. CUserCmd — полностью подтверждённый путь

```
client + dwLocalPlayerController → controller
GetUserCmdBase(controller)                        // pattern см. §6
  → base кольца; sequence = *(int*)(base+0x5910)
GetUserCmd(controller, sequence)                  // = GetUserCmdBase - 0x90 (подтв. Ghidra FUN_180902B00)
  → cmd = base + (sequence % 150) * 0x98
  → проверка живости: *(int*)(cmd+8) == sequence
```

**Раскладка команды (stride 0x98):**
- +0x08 — sequence (int)
- +0x40 — raw-указатель на CBaseUserCmdPB (protobuf)
- +0x58 — похоже, статическая protobuf-арена (кандидат, см. §7)
- **+0x60 — buttons value (uint64); IN_JUMP = 0x2** (подтверждено differential-пробой: SPACE зажат → 0x2)
- +0x68 — buttons changed (при снятии IN_JUMP туда пишем 0x2 — переход)
- +0x70 — buttons scroll (не используется)

Подтверждение +0x60/+0x68 делалось так: read-only лог `jump diff:` раз в 750мс с space=0/1 — биты точно совпадали с физическим SPACE (на 14176; на 14178 offsets не менялись, diag v70 подтвердил q60/q68 живые).

---

## 5. dwCSGOInput — главная ловушка

**`client + dwCSGOInput` — это УЖЕ сам объект CCSGOInput, указатель НЕ разыменовывать.**
(Стандартная ошибка из туториалов — отсюда «vtable из байтов машинного кода» в логах.)

- Первые 8 слотов объекта (slots[0..7]) — function pointers. **slot 5 = CreateMove** (hook через MinHook, сигнатура `void fastcall(input*, int slot, bool active)`).
- Pattern-путь Velocity `FFFFFFFF 488D05 *... +28~` — raw match есть в логе, но кастомный decode (+28~) так и не воспроизведён; хук по slot 5 работает.
- ВАЖНО: параметр `active` может приходить false на релевантных тиках — НЕ гейтить на нём (иначе bhop молчит).
- ВАЖНО-2: в TryHookCreateMove offset обязан браться из живых `off.dwCSGOInput` — зашитый 0x23BFB20 (14176) после апдейта молча убил хук (фикс fa7ed74, v70).

История: бхоп «не работал вообще» после апдейта 14177 именно из-за зашитого старого offset в этой одной функции.

---

## 6. Pattern-сканер (usercmd_probe.hpp) — что ищем и статус на 14178

Все найдены и живы (лог `usercmd probe: ... (apply path found)`):
- GetUserCmdBase: `48 83 EC 28 E8 ? ? ? ? 8B 80 10 59 00 00` → call target
- GetUserCmd: sibling = GetUserCmdBase − 0x90
- subtick_move_alloc: `48 8B 54 CA 08 8D 41 01 89 47 08 EB 16 48 8B 0F >E8...< 48 8B D0 48 8B CF`
- utl_vector_push: `>E8 ? ? ? ?< 4C 8B D0 45 8B 4A 10`
- string_copy: `>E8 ? ? ? ?< 0F 10 45 88`
- serialize_move_crc: `48 89 5C 24 ? 55 56 57 48 83 EC 30 49 8B C0 48 8B FA 48 8B F1 48 8B 09 F6 C1 03`
- NoSpread (из UGame-исходника): computeRandomSeed `48 89 5C 24 ? 57 48 81 EC ? ? ? ? 48 8B F9 41 8B ?`, calculateSpread `48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 63 EA` — статус на 14178 неизвестен, в логе есть строка `nospread probe: seed=... spread=...`.
- V16-фикс добавлял: button_state_alloc (не портирован), csgo_input pattern `84C0740C488D0D*...` (не нужен — есть dwCSGOInput).

Сканер умеет: byte+mask по образу модуля, resolve RIP-relative call (`>` в velocity-нотации = target E8).

---

## 7. Protobuf usercmd (pb_cmd.hpp) — РАБОЧИЙ, основа silent aim

Источник раскладки: официальные PB-хедеры cspatterns.dev/CUserCMD (пользователь приносил текстом; совпадают с usercmd.proto/cs_usercmd.proto из GameTracking). Каждое сообщение: PBMessage{vtable,metadata}=0x10, потом has_bits(u32), cached_size(u32), потом поля.

Цепочка от cmd (stride 0x98):
```
cmd+0x40 → raw CBaseUserCmdPB; impl = raw+0x10
  base impl+0x08 → repeated subtick_move_step {arena,current_size,total_size,rep}
                   rep: {allocated(int), elements[] с +0x08}; шаг impl=+0x10, 0x28 байт:
                   bits+0x00, button+0x08(u64), pressed+0x10(bool), when+0x14(float, СЕКУНДЫ!),
                   analog_forward+0x18, analog_left+0x1C
  base impl+0x28 → CInButtonStatePB* {bits, s1+0x08, s2+0x10, s3+0x18} (u64 маски)
  base impl+0x30 → CMsgQAngle* {bits, x+0x08, y+0x0C, z+0x10}
```
has_bits: base: buttons=0x2, viewangles=0x4, move_crc=0x1; button states 1/2/3=0x1/0x2/0x4; шаг: все поля=0x3F.

**Viewangles probe ПОДТВЕРДИЛ запись** (лог v72): `pb: 14.000015/129.999847` == живым dwViewAngles. Silent aim (`pbcmd::WriteViewAngles`) пишет только сюда, камеру не трогает — включён, статус «в бою не проверен до конца».

CRC (V16 serialize): ручная упаковка protobuf-байт (0x1A=buttons, 0x22=angles, wire-теги 0x09/0x11/0x19, 0x0D/0x15/0x1D) → string_copy → serialize_move_crc в base+0x20 с arena. Arena ищется: baseRaw+0x08 (битовая маска ±1/±3) → fallback cmd+0x18 → **кандидат cmd+0x58 (из diag, НЕ проверен)**; если нет — `crc skipped` (не фатально, кнопки уже записаны).

Stages ApplyButtons: 0=OK, 3=noBase, 5=noButtons, 7=noArena, 8=OK-noCRC.

---

## 8. Bhop — состояние и вся история

**VeloBhop [F4]** (удерживать SPACE): CreateMove hook (slot 5, §5) → GetUserCmd → в воздухе:
IN_JUMP снимается в +0x60, transition в +0x68, ApplyButtons (protobuf+CRC). На тике приземления — subtick-пара release(curtime-frametime)/press(curtime).
Статус: прыгает «криво/через раз». Причины по логам:
- `stage 8` (crc skipped) — арена не найдена, CRC не считается (возможно, сервер перетирает кнопки);
- `landing subtick pair unavailable` — та же арена для выделения шагов.
**СЛЕДУЮЩИЙ ШАГ: попробовать cmd+0x58 как arena (см. §7).**

История: SendInput-вариант не работал (физический SPACE перекрывал synthetic); запись из feature loop перетиралась игрой (нужен именно CreateMove-тайминг); VirtualProtect на каждую запись ронял FPS до 5 (фикс: ReadFast/WriteFast + валидация один раз за тик, v65).

**ExtHope (hold X)**: автономный, НЕ зависит от usercmd — SendInput-дробь кликов SPACE, пока зажат X, рейтинг 1..128/сек (слайдер). Так делают экстерналы. Работает всегда.

Разбор CCSMovementServices::CCSPlayerModernJump::BunnyHope (юзер принес с UGame): CanJump = landed>press (или по fractions); **sv_jump_spam_penalty_time** — штраф за спам (вот почему одиночные прессы через раз); movement services+0x80 = 3×u64 button state; subtick `when` = curtime-секунды, НЕ фракция тика (V16-эвристика устарела).

---

## 9. Silent aim — почти готов

`g_features.silentAim` + чекбокс в меню. Пишет углы через pbcmd::WriteViewAngles (§7), прямую запись viewAngles блокирует. В бой не проверен. Если пули летят мимо при живом логе `silent: углы записаны...` — сервер берёт углы ещё откуда-то (input_history / перетирание до отправки) — копать в эту сторону.

---

## 10. NoSpread (nospread.hpp)

UGame-схема: перебор pitch 0.125°×768 → seed = ComputeRandomSeed(pawn,angles,tick) → spread = CalculateSpread(itemDef,1,0,seed+1,inacc,spread) → компенсация pitch+=deg(|spread|), yaw-=deg(atan2(sx,sy)) → верификация seed и spread-дельты <0.0005. itemDefIndex читается из vdata+0x1BA (uint16). Подключён к aim (128 итераций) и ragebot (256). Паттерны на 14178 не подтверждены — гейт `ReadyForNoSpread`, при unavailable просто no-op. inaccuracy/spread пока заглушка 0.01/0.01 — надо брать реальные из weapon vdata (m_flSpread=0x758 и т.п. есть в offsets.hpp).

---

## 11. ESP (gui.cpp DrawEsp)

20 Гц скан (chrono::steady_clock, 50мс) с кешем; только при entityLayoutVerified; враги (team!=local), IsAlive, дистанция ≤3000 юнитов; сортировка дальние-снизу-слоём; W2S по dwViewMatrix (w<0.65 отсечка); бокс 0.38×height, цвет=HP(красный→зелёный), тень, HP-бар слева, дистанция «Nм» (юниты/52.49) с тумблером. Рисуется в Present через GetBackgroundDrawList.
История: был скан каждый кадр → сильные лаги; 10Гц; теперь 20Гц — ОК.
Если боксов нет: смотреть `entity-list:` в логе (layout подтверждён?) и что в матче есть враги (тиммейтов не рисуем).

---

## 12. Меню (MemeSense-style, v78)

Полная структура: RGB-полоска 2px (анимация по hue), заголовок + Save, sidebar 13 вкладок c Font Awesome Solid 900 (извлечён из fa.h MemeSense-зипа в assets/fa_solid.hpp, 414КБ, грузится ТОЛЬКО на диапазон U+E000..U+F8FF), активная вкладка — красная полоса слева.
Вкладки: Legitbot/AimAssist (= Combat: aim speed 30-8000, smooth, updates/sec 1-120, prediction, trigger, silent, nospread), Players/Chams (диагностика), Items/Visuals (recoil/gamesense), World/View (ESP), Indicators, Misc (antiaim+spin, clantag, ragebot+все его слайдеры), Movement (VeloBhop/ExtHope), Inventory, Configs (unload).
**Баг «иконки вместо текста» — разобран:** в fonts.zip оба ttf оказались обычными текстовыми шрифтами (cmap только ASCII); иконки теперь — настоящий FA, текст — Segoe/Arial с кириллицей, FontDefault на тексте.
Клавиши: P/INSERT меню, END unload, F1 aim on/off (режим — в меню), F2 antiaim, F3 recoil, F4 VeloBhop, F5 gamesense, F6 ragebot, X — ExtHope.

---

## 13. Clantag

VEngineCvar007 (tier0.dll CreateInterface) → cvar «name» (linked-список entries, FNV-поиск не нужен — обычный strcmp-обход) → базовый ник; Source2EngineToClient001 → vtable[40] ExecuteClientCmd → `setinfo name "..."`. Анимация [ → [N → ... → [NeverWin] (250мс/кадр, 3с пауза, обратный разбор). Оба пути подтверждены логом (engine command path is active). Ник «VAC TEST» захватился корректно; кириллица в логе отображается криво (UTF-8 в wchar-лог) — косметика, в игру уходит правильно.

---

## 14. Инжектор и выгрузка

Инжектор: тулснап-поиск cs2.exe → VirtualAllocEx+WriteProcessMemory+CreateRemoteThread(LoadLibraryW) → проверка IsModuleLoaded. Работает. Пользователь иногда инжектит «экстернал-лоадером» — DLL тогда не находит neverwin.ini рядом (не критично: встроенные оффсеты актуальны).
Выгрузка: END/кнопка → g_unloadRequested → Present-хук снимает всё сам (WNDPROC, MinHook, vtable swapchain) → event → FreeLibraryAndExitThread. Известные риски (не чинились): таймаут 3с при свёрнутой игре; vtable-восстановление через trampoline-адреса MinHook.

---

## 15. Ключевые источники-доноры в репо

- `velocity-fixed-main hackvshack.net.zip` (пароль hvh.net!) — V16-фикс: правильный input.apply (button_state_alloc, arena по битам, CRC), актуальные тогда паттерны. Главное, что взяли: механика apply+CRC, безопасные get_current_cmd.
- `CHEAT.zip` — CGameEntitySystem (stride 0x70!), bone cache (+0x150/+0x80/0x20/head=7), ExecuteClientCmd vtable[40], vECGOInput-структура.
- `velocity/` (ориг) — CreateMove-паттерн, usercmd-структуры, bhop-концепция.
- `quint/` — bunny_hop по кнопкам, engine cvar walk, clantag-идея.
- cspatterns.dev/CUserCMD — официальные PB-структуры (главный источник §7).
- UGame-пост — NoSpread сигнатуры + разбор BunnyHope/penalty.
- GameTracking-CS2 — proto-файлы для сверки.

---

## 16. Git/процесс — ВАЖНО для следующих сессий

Ветка: `arena/01a024b7-neverwin-cs2` (только она!). Пользователь сам коммитит в неё файлами (Add files via upload) — часто ПОПУТНО с нашей сборкой.
**Регулярно случается**: локальная копия откатывается к древнему коммиту (5286d2b). Лечение: `git fetch origin arena/01a024b7-neverwin-cs2 && git reset --hard FETCH_HEAD`.
Push-конфликты: сначала `git stash push -u`, `git rebase FETCH_HEAD`, при конфликтах `git checkout --theirs <наши файлы>` → add → `GIT_EDITOR=true git rebase --continue` → push. Не пушить force.
Крупные файлы (dll/exe/zip) в репо — норма для этого проекта, так заведено.

## 17. Что делать дальше (по приоритету)

1. **Bhop-стабильность**: подставить cmd+0x58 как arena-fallback в AcquireSubtickStep/CRC (диаг v70: q58=стабильный ptr) → если subtick-пара заработает, убрать «crc skipped».
2. **Silent aim**: боевой тест; при промахах — исследовать input_history (CSGOInputHistoryEntryPB, тоже в PB-хедерах).
3. **Entity layout**: если снова fallback после следующего апдейта — сначала stride-кандидаты 0x70/0x78/0x80 + m_hPawn offset (меняется!), потом Ghidra.
4. NoSpread: реальные inaccuracy/spread из weapon vdata вместо 0.01.
5. Меню: Save → реальная сериализация конфига; Grenade Helper-вкладка если нужна.
6. Автоперенос оффсетов: при апдейте CS2 — cs2-dumper → output.zip в ветку → diff dw-оффсетов → offsets.hpp+ini+сборка. dw-меняются ВСЕГДА, schema почти никогда; m_hPawn менялся (0x6BC→0x600) — проверять тоже.
