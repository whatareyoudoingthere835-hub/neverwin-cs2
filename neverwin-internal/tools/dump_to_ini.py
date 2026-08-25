#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генератор neverwin.ini из дампа cs2-dumper.

Зачем: CS2 обновляется почти каждую неделю, и оффсеты в коде устаревают.
cs2-dumper (https://github.com/a2x/cs2-dumper) снимает свежие значения
с локального клиента. Этот скрипт превращает его output в ini, который
DLL читает при инжекте — перекомпилировать ничего не надо.

Использование:
    python dump_to_ini.py <папка с дампом cs2-dumper> [куда положить neverwin.ini]

По умолчанию ini пишется в папку дампа — положи его рядом с neverwin.dll.
Если ключа в дампе нет — скрипт предупредит и пропустит его; DLL подставит
встроенное значение (см. src/offsets.hpp).
"""

import json
import os
import re
import sys

# Сетевая часть: модуль client.dll
NET_KEYS = ("dwEntityList", "dwLocalPlayerPawn", "dwViewAngles",
            "dwLocalPlayerController", "dwCSGOInput")

# Схема: класс -> ключи. Поиск СТРОГО внутри класса — многие имена полей
# (m_fFlags, m_iHealth, ...) встречаются в разных классах с разными оффсетами,
# и глобальный «первый попавшийся» даёт чужое значение. Оффсеты плоские:
# pawn наследует всю цепочку, поэтому класс-владелец поля не важен для чтения
# pawn + offset, важно не перепутать само значение.
CLASS_KEYS = {
    "C_BaseEntity":           ("m_iHealth", "m_lifeState", "m_iTeamNum", "m_fFlags", "m_pGameSceneNode"),
    "C_BaseModelEntity":      ("m_vecViewOffset",),
    "CGameSceneNode":         ("m_vecAbsOrigin", "m_bDormant"),
    "C_BasePlayerPawn":       ("m_pCameraServices", "m_pWeaponServices", "m_pMovementServices", "m_hController"),
    "CPlayer_MovementServices": ("m_nLastCommandNumberProcessed", "m_flCmdForwardMove", "m_flCmdLeftMove", "m_vecLastMovementImpulses"),
    "C_CSPlayerPawn":         ("m_bWaitForNoAttack", "m_bGunGameImmunity", "m_iIDEntIndex"),
    "CCSPlayerController":    ("m_hPlayerPawn", "m_bPawnIsAlive"),
    "CBasePlayerController":  ("m_nTickBase", "m_CommandContext", "m_hPawn"),
    "CPlayer_CameraServices": ("m_vecCsViewPunchAngle",),
    "CPlayer_WeaponServices": ("m_hActiveWeapon",),
    "C_BasePlayerWeapon":     ("m_iClip1", "m_nNextPrimaryAttackTick"),
    "C_CSWeaponBase":         ("m_bInReload",),
}


def find_in_json(obj, key, out):
    """Рекурсивный поиск первого вхождения ключа (int) в структуре json."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == key and isinstance(v, int):
                out.append(v)
                return
            find_in_json(v, key, out)
            if out:
                return
    elif isinstance(obj, list):
        for v in obj:
            find_in_json(v, key, out)
            if out:
                return


def find_in_class(obj, cls, field, out):
    """Ищет field внутри узла класса cls (значение узла — dict/list)."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == cls and isinstance(v, (dict, list)):
                find_in_json(v, field, out)
                if out:
                    return
            find_in_class(v, cls, field, out)
            if out:
                return
    elif isinstance(obj, list):
        for v in obj:
            find_in_class(v, cls, field, out)
            if out:
                return


def load_json(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return json.load(f)


def net_from_json(path):
    result = {}
    try:
        data = load_json(path)
    except (OSError, ValueError) as e:
        print(f"  [!] {os.path.basename(path)}: {e}")
        return result
    for key in NET_KEYS:
        found = []
        find_in_json(data, key, found)
        if found:
            result[key] = found[0]
    return result


def schema_from_json(path, cls, keys):
    """Ищет ключи строго внутри класса cls. Фолбэк на глобальный поиск —
    с предупреждением (лучше предупредить, чем тихо взять чужое поле)."""
    result = {}
    try:
        data = load_json(path)
    except (OSError, ValueError) as e:
        print(f"  [!] {os.path.basename(path)}: {e}")
        return result
    for key in keys:
        found = []
        find_in_class(data, cls, key, found)
        if not found:
            find_in_json(data, key, found)
            if found:
                print(f"  [!] {key}: класс {cls} в дампе не найден, "
                      f"взято первое вхождение — проверь значение")
        if found:
            result[key] = found[0]
    return result


def schema_from_hpp(path, cls, keys):
    result = {}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print(f"  [!] {os.path.basename(path)}: {e}")
        return result

    # У дамперов класс лежит как:
    #   namespace C_CSPlayerPawnBase { constexpr std::ptrdiff_t m_... = 0x...; }
    m = re.search(rf"namespace\s+{re.escape(cls)}\s*\{{(.*?)\n\}}", text, re.S)
    if not m:
        print(f"  [!] {os.path.basename(path)}: класс {cls} не найден")
        return result
    block = m.group(1)
    for key in keys:
        fm = re.search(rf"\b{re.escape(key)}\s*=\s*(0[xX][0-9a-fA-F]+)", block)
        if fm:
            result[key] = int(fm.group(1), 16)
    return result


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    dump_dir = sys.argv[1]
    ini_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(dump_dir, "neverwin.ini")

    if not os.path.isdir(dump_dir):
        print(f"[!] Папка не найдена: {dump_dir}")
        return 1

    print(f"[*] Папка дампа: {dump_dir}")
    print(f"[*] Куда пишем:  {ini_path}")

    net = {}
    schema = {}

    # 1) Сетевая часть: offsets.json, иначе любой другой json с нужными ключами.
    offsets_json = os.path.join(dump_dir, "offsets.json")
    if os.path.isfile(offsets_json):
        net = net_from_json(offsets_json)
    for name in sorted(os.listdir(dump_dir)):
        if len(net) == len(NET_KEYS):
            break
        if name.lower() == "offsets.json" or not name.lower().endswith(".json"):
            continue
        net.update(net_from_json(os.path.join(dump_dir, name)))

    # 2) Схема: client_dll.json, иначе client_dll.hpp / client.dll.hpp.
    p = os.path.join(dump_dir, "client_dll.json")
    if os.path.isfile(p):
        for cls, keys in CLASS_KEYS.items():
            schema.update(schema_from_json(p, cls, keys))
    if not schema:
        for name in ("client_dll.hpp", "client.dll.hpp"):
            p = os.path.join(dump_dir, name)
            if os.path.isfile(p):
                for cls, keys in CLASS_KEYS.items():
                    schema.update(schema_from_hpp(p, cls, keys))
                if schema:
                    break

    # 3) Пишем ini.
    lines = ["[offsets]"]
    for key in NET_KEYS:
        if key in net:
            lines.append(f"{key}=0x{net[key]:X}")
    for cls, keys in CLASS_KEYS.items():
        for key in keys:
            if key in schema:
                lines.append(f"{key}=0x{schema[key]:X}")

    with open(ini_path, "w", encoding="ascii") as f:
        f.write("\n".join(lines) + "\n")

    # 4) Отчёт.
    total = len(NET_KEYS) + sum(len(keys) for keys in CLASS_KEYS.values())
    missing = [k for k in NET_KEYS if k not in net]
    for cls, keys in CLASS_KEYS.items():
        missing += [f"{cls}::{k}" for k in keys if k not in schema]
    found = len(net) + len(schema)
    print(f"[+] Записано {found}/{total} ключей: {ini_path}")
    if missing:
        print(f"[!] Не найдено в дампе: {', '.join(missing)} — DLL возьмёт встроенные значения.")
        print("[!] Проверь, что дамп снят с актуального клиента (cs2-dumper при запущенной CS2).")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
