# QUINT — сборка с оффсетами из дампа build 14176

Исходник: `quintcs2.zip` от passtuh. Код НЕ менялся — обновлены только
оффсеты/глобалки под дамп 14176 (20.08.2026) и три compiler-совместимости
для кросс-сборки (см. ниже).

## Что заменено (только оффсеты)

| Что | Было | Стало |
|---|---|---|
| CSGOInput | сигнатура `E8 ? ? ? ? 48 8B 93...` | `client.dll + 0x23BFB20` (dwCSGOInput) |
| GlobalVars | сигнатура `48 89 15...` | `client.dll + 0x2095D48` (dwGlobalVars) |
| EntitySystem | сигнатура `48 8B 0D...` | `client.dll + 0x2555050` (dwEntityList) |
| get_player_pawn | сигнатура-функция | `client.dll + 0x23AA118` (dwLocalPlayerPawn) |
| get_player_controller | сигнатура-функция | `client.dll + 0x2384DB0` (dwLocalPlayerController) |
| get/set_view_angle | сигнатуры-функции | `client.dll + 0x23C01A8` (dwViewAngles) |
| get_base_entity | сигнатура-функция | сырой обход списка (их же закомментированный код: +0x10/8, stride 0x78) |

Схемные поля quint резолвит в рантайме через SchemaSystem — им обновление
не нужно. Свопчейн-сигнатура (`48 89 2D E4 21 46 00...`) совпадает с текущим
клиентом и оставлена. Остальные функциональные сигнатуры не трогались —
в дампе их нет, это не оффсеты.

## Отличия сборки (не код)

- mingw/clang вместо MSVC: Windows.h→shim, Includes.h/Entry.h shim-ы,
  `(void*)`-касты указателей на функции, `static` у g_hooks в minhook
  (коллизия с quint g_hooks), `-x c` для lz4.h.
- freetype.lib/lz4.lib собраны MSVC — CRT-шим (cookie в no-op, _setjmp на asm).

## Сборка

```
bash quint/build_release.sh        # quint_vN.dll + quint_injector_vN.exe
```
Объекты кэшируются в quint/.build. Меню — INSERT, как у квинта.
