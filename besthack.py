import os
import time
import math
import random
import ctypes
import threading
import keyboard
from pymem import Pymem
from pymem.process import module_from_name

# --- Оффсеты движка (дамп build 14176, 20.08.2026) ---
dwEntityList = 0x2555050
dwLocalPlayerPawn = 0x23AA118
dwViewAngles = 0x23C01A8

# --- Оффсеты схем ---
m_iHealth = 0x34C
m_lifeState = 0x354   # 0=жив, 1=умирает, 2=мёртв
m_iTeamNum = 0x3E7
m_fFlags = 0x3F4
m_iClip1 = 0x1700
m_bInReload = 0x1814

# --- Позиции (реверс аимбот) ---
m_pGameSceneNode = 0x330    # C_BaseEntity -> CGameSceneNode*
m_vecAbsOrigin = 0xC8       # CGameSceneNode -> Vector
m_bDormant = 0x103          # CGameSceneNode -> bool (дормант)
m_vecViewOffset = 0xE78     # C_BaseModelEntity -> Vector (высота глаз)

# --- Сервисы ---
m_pCameraServices = 0x1240      # C_BasePlayerPawn -> CPlayer_CameraServices*
m_vecCsViewPunchAngle = 0x48    # CPlayer_CameraServices -> QAngle (панч отдачи)
m_pWeaponServices = 0x1208      # C_BasePlayerPawn -> CPlayer_WeaponServices*
m_hActiveWeapon = 0x60          # CPlayer_WeaponServices -> CHandle (u32)

# --- Настройки меню ---
features = {
    "antiaimbot": False,
    "antiaimless": False,
    "visrecoil": False,
    "antibhop": False,
    "spinspeed": 1.0   # 0..10: множитель спинбота F2 (0 — без кручения)
}

def clear_console():
    os.system('cls' if os.name == 'nt' else 'clear')

def draw_menu():
    clear_console()
    print("=== NEVERWIN (Python Edition) ===")
    print(f"[F1] Реверс аим raimv1 (наводка на тимейтов) : {'ON' if features['antiaimbot'] else 'OFF'}")
    print(f"[F2] Антиаимлесс (смотреть в пол)  : {'ON' if features['antiaimless'] else 'OFF'}")
    print(f"[F3] Visual Recoil (+400%)         : {'ON' if features['visrecoil'] else 'OFF'}")
    print(f"[F4] Антибхоп                      : {'ON' if features['antibhop'] else 'OFF'}")
    print(f"[F7/F8] Скорость спинбота          : x{features['spinspeed']:.0f}")

    print(f"[--] Gamesense (Дроп оружия)       : ALWAYS ON (Пассивный дебафф 20%)")
    print("=================================")

def toggle_feature(feat_name):
    features[feat_name] = not features[feat_name]
    draw_menu()

keyboard.on_press_key("F1", lambda _: toggle_feature("antiaimbot"))
keyboard.on_press_key("F2", lambda _: toggle_feature("antiaimless"))
keyboard.on_press_key("F3", lambda _: toggle_feature("visrecoil"))
keyboard.on_press_key("F4", lambda _: toggle_feature("antibhop"))

def spin_speed_down(_):
    features["spinspeed"] = max(0, features["spinspeed"] - 1)
    draw_menu()

def spin_speed_up(_):
    features["spinspeed"] = min(10, features["spinspeed"] + 1)
    draw_menu()

keyboard.on_press_key("F7", spin_speed_down)
keyboard.on_press_key("F8", spin_speed_up)



def iter_pawns(pm, entity_list):
    # Полный обход энтити-листа: ВСЕ 64 блока по 512 слотов (32768 слотов).
    # Раньше было 32 блока — теряли игроков с хэндлами >= 0x4000 (блоки 32..63).
    # Именно из-за этого было «5 тиммейтов, видно 1».
    # Страйд 0x78 = 120 байт — как в C++ версии (entities.hpp).
    for block in range(64):
        try:
            list_entry = pm.read_longlong(entity_list + 0x10 + 8 * block)
        except Exception:
            continue
        if not list_entry:
            continue
        for idx in range(512):
            try:
                pawn = pm.read_longlong(list_entry + 120 * idx)
            except Exception:
                continue
            if pawn:
                yield pawn

def get_entity_by_handle(pm, client_base, entity_list, handle):
    # Парсинг энтити листа Source 2 по хэндлу
    index = handle & 0x7FFF
    list_entry = pm.read_longlong(entity_list + 0x10 + (8 * (index >> 9)))
    if not list_entry:
        return 0
    return pm.read_longlong(list_entry + 120 * (index & 0x1FF))

def neverwin_loop():
    try:
        pm = Pymem("cs2.exe")
        client = module_from_name(pm.process_handle, "client.dll").lpBaseOfDll
    except Exception as e:
        print("[-] CS2 не найдена. Запусти игру.")
        return

    print("[+] CS2 найдена, инжект памяти успешен.")
    time.sleep(1)
    draw_menu()

    previous_ammo = -1

    while True:
        time.sleep(0.001) # Чтобы не грузить процессор на 100%

        try:
            local_player = pm.read_longlong(client + dwLocalPlayerPawn)
            if not local_player:
                continue
                
            local_health = pm.read_int(local_player + m_iHealth)
            if local_health <= 0:
                continue
            # Мёртвый локальный: труп под камерой смерти — цель в зенит.
            if pm.read_uint(local_player + m_lifeState) & 0xFF:
                continue

            local_team = pm.read_uint(local_player + m_iTeamNum) & 0xFF  # uint8!
            entity_list = pm.read_longlong(client + dwEntityList)

            # --- 1. АНТИБХОП ---
            if features["antibhop"] and keyboard.is_pressed("space"):
                flags = pm.read_uint(local_player + m_fFlags)
                if flags & 1: # Если FL_ONGROUND
                    # Снимаем флаг нахождения на земле
                    pm.write_uint(local_player + m_fFlags, flags & ~1)

            # --- 2. GAMESENSE (Дроп оружия) ---
            # Оружие через сервисы: pawn -> m_pWeaponServices -> m_hActiveWeapon
            weapon_services = pm.read_longlong(local_player + m_pWeaponServices)
            if weapon_services:
                weapon_handle = pm.read_uint(weapon_services + m_hActiveWeapon)
                if weapon_handle:
                    weapon_entity = get_entity_by_handle(pm, client, entity_list, weapon_handle)
                    if weapon_entity:
                        current_ammo = pm.read_int(weapon_entity + m_iClip1)
                        is_reloading = pm.read_bool(weapon_entity + m_bInReload)

                        if (previous_ammo != -1 and current_ammo < previous_ammo) or is_reloading:
                            if random.randint(1, 100) <= 20: # 20% шанс
                                # Симуляция нажатия G
                                ctypes.windll.user32.keybd_event(0x47, 0, 0, 0)
                                ctypes.windll.user32.keybd_event(0x47, 0, 2, 0)
                                time.sleep(0.3)
                        previous_ammo = current_ammo

            # --- 3. VISUAL RECOIL (+400%) ---
            # Панч отдачи теперь в camera services: m_vecCsViewPunchAngle
            if features["visrecoil"]:
                punch_x = 0.0
                punch_y = 0.0
                cam_services = pm.read_longlong(local_player + m_pCameraServices)
                if cam_services:
                    punch_x = pm.read_float(cam_services + m_vecCsViewPunchAngle)
                    punch_y = pm.read_float(cam_services + m_vecCsViewPunchAngle + 4)

                if punch_x != 0.0 or punch_y != 0.0:
                    view_x = pm.read_float(client + dwViewAngles)
                    view_y = pm.read_float(client + dwViewAngles + 4)
                    
                    # Отдача в CS2 работает в минус, умножаем ее и вычитаем из камеры
                    new_x = view_x - (punch_x * 4.0)
                    new_y = view_y - (punch_y * 4.0)

                    # Защита от перекрута
                    if new_x > 89.0: new_x = 89.0
                    if new_x < -89.0: new_x = -89.0
                    
                    pm.write_float(client + dwViewAngles, new_x)
                    pm.write_float(client + dwViewAngles + 4, new_y)

            # --- 4a. РЕВЕРС АИМ raimv1 (наводка на ближайшего тиммейта) ---
            # Стены не проверяются, дальность не ограничена. Живой тиммейт
            # предпочтительнее трупа: если живых нет — цель падает на
            # ближайший труп тиммейта (пока он в списке).
            if features["antiaimbot"]:
                eye = [0.0, 0.0, 0.0]
                scene_node = pm.read_longlong(local_player + m_pGameSceneNode)
                if scene_node:
                    eye = [
                        pm.read_float(scene_node + m_vecAbsOrigin),
                        pm.read_float(scene_node + m_vecAbsOrigin + 4),
                        pm.read_float(scene_node + m_vecAbsOrigin + 8),
                    ]
                # m_vecViewOffset — CNetworkViewOffsetVector (сетевой тип):
                # при стухшем оффсете отдаёт мусор (в z уводило прицел в зенит).
                # Санитайз: вылет за диапазоны — фолбэк 0/0/64.
                vox = pm.read_float(local_player + m_vecViewOffset)
                voy = pm.read_float(local_player + m_vecViewOffset + 4)
                voz = pm.read_float(local_player + m_vecViewOffset + 8)
                if not (-100.0 <= vox <= 100.0): vox = 0.0
                if not (-100.0 <= voy <= 100.0): voy = 0.0
                if not (-200.0 <= voz <= 300.0): voz = 64.0
                eye[0] += vox
                eye[1] += voy
                eye[2] += voz

                if eye != [0.0, 0.0, 0.0]:
                    best_alive = None
                    best_alive_dist = float("inf")
                    best_any = None
                    best_any_dist = float("inf")
                    for pawn in iter_pawns(pm, entity_list):
                        if pawn == local_player:
                            continue
                        try:
                            if (pm.read_uint(pawn + m_iTeamNum) & 0xFF) != local_team:  # uint8
                                continue
                        except Exception:
                            continue

                        try:
                            node = pm.read_longlong(pawn + m_pGameSceneNode)
                        except Exception:
                            continue
                        if not node:
                            continue
                        # Дормант — скипаем, иначе хп=0 и считаем всех мертвыми
                        try:
                            if pm.read_bool(node + m_bDormant):
                                continue
                        except Exception:
                            pass
                        try:
                            origin = [
                                pm.read_float(node + m_vecAbsOrigin),
                                pm.read_float(node + m_vecAbsOrigin + 4),
                                pm.read_float(node + m_vecAbsOrigin + 8),
                            ]
                        except Exception:
                            continue
                        if origin == [0.0, 0.0, 0.0]:
                            continue

                        dx = origin[0] - eye[0]
                        dy = origin[1] - eye[1]
                        dz = origin[2] - eye[2]
                        dist2 = dx * dx + dy * dy + dz * dz

                        # Свой труп под ногами (<64) — вертикальные углы, пропускаем.
                        if dist2 < 64.0 * 64.0:
                            continue

                        try:
                            life = pm.read_uint(pawn + m_lifeState) & 0xFF
                            hp = pm.read_int(pawn + m_iHealth)
                        except Exception:
                            continue
                        # Живой только если lifeState==0 и hp>0 и hp<=1000
                        if life != 0 or hp <= 0 or hp > 1000:
                            continue
                        if dist2 < best_alive_dist:
                            best_alive_dist = dist2
                            best_alive = origin
                        if dist2 < best_any_dist:
                            best_any_dist = dist2
                            best_any = origin

                    # Цель — только ЖИВОЙ тиммейт (hp > 0). Трупы не берём:
                    # прицел вёл на мертвецов, когда живых не было.
                    best_origin = best_alive

                    if best_origin:
                        dx = best_origin[0] - eye[0]
                        dy = best_origin[1] - eye[1]
                        dz = (best_origin[2] + 64.0) - eye[2]  # корпус/голова
                        dist2d = max(math.hypot(dx, dy), 1.0)
                        pitch = math.degrees(math.atan2(-dz, dist2d))
                        yaw = math.degrees(math.atan2(dy, dx))
                        pitch = max(-89.0, min(89.0, pitch))
                        while yaw > 180.0: yaw -= 360.0
                        while yaw < -180.0: yaw += 360.0
                        pm.write_float(client + dwViewAngles, pitch)
                        pm.write_float(client + dwViewAngles + 4, yaw)

            # --- 4b. АНТИАИМЛЕСС (виден враг — смотрим в пол) ---
            if features["antiaimless"]:
                enemy_spotted = False
                for pawn in iter_pawns(pm, entity_list):
                    if pawn == local_player:
                        continue
                    try:
                        node = pm.read_longlong(pawn + m_pGameSceneNode)
                        if node and pm.read_bool(node + m_bDormant):
                            continue
                        if pm.read_uint(pawn + m_lifeState) & 0xFF:
                            continue
                        health = pm.read_int(pawn + m_iHealth)
                        if health <= 0 or health > 1000:
                            continue
                        team = pm.read_uint(pawn + m_iTeamNum) & 0xFF  # uint8
                        if team == 0 or team == local_team:
                            continue
                    except Exception:
                        continue

                    enemy_spotted = True
                    break

                if enemy_spotted:
                    # Жестко уводим в пол
                    pm.write_float(client + dwViewAngles, 89.0)
                    view_y = pm.read_float(client + dwViewAngles + 4)
                    new_y = view_y + 10.0 * features["spinspeed"]
                    if new_y > 180.0: new_y -= 360.0
                    pm.write_float(client + dwViewAngles + 4, new_y)

        except Exception:
            # Игнорим ошибки чтения (например, во время загрузки карты)
            pass

if __name__ == "__main__":
    draw_menu()
    threading.Thread(target=neverwin_loop, daemon=True).start()
    keyboard.wait("delete") # Кнопка для полного закрытия софта