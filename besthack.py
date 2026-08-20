import os
import time
import random
import ctypes
import threading
import keyboard
from pymem import Pymem
from pymem.process import module_from_name

# --- Оффсеты движка ---
dwEntityList = 0x2554050
dwLocalPlayerPawn = 0x23A9118
dwViewAngles = 0x23BF1A8

# --- Оффсеты схем ---
m_iHealth = 0x34C
m_iTeamNum = 0x3E7
m_fFlags = 0x3F4
m_aimPunchAngle = 0x14CC
m_pClippingWeapon = 0x1308
m_iClip1 = 0x15A4
m_bInReload = 0x1704

# --- Настройки меню ---
features = {
    "antiaimbot": False,
    "antiaimless": False,
    "visrecoil": False,
    "antibhop": False
}

def clear_console():
    os.system('cls' if os.name == 'nt' else 'clear')

def draw_menu():
    clear_console()
    print("=== NEVERWIN (Python Edition) ===")
    print(f"[F1] Антиаимбот (тряска от врагов) : {'ON' if features['antiaimbot'] else 'OFF'}")
    print(f"[F2] Антиаимлесс (смотреть в пол)  : {'ON' if features['antiaimless'] else 'OFF'}")
    print(f"[F3] Visual Recoil (+400%)         : {'ON' if features['visrecoil'] else 'OFF'}")
    print(f"[F4] Антибхоп                      : {'ON' if features['antibhop'] else 'OFF'}")
    print(f"[--] Gamesense (Дроп оружия)       : ALWAYS ON (Пассивный дебафф 20%)")
    print("=================================")

def toggle_feature(feat_name):
    features[feat_name] = not features[feat_name]
    draw_menu()

keyboard.on_press_key("F1", lambda _: toggle_feature("antiaimbot"))
keyboard.on_press_key("F2", lambda _: toggle_feature("antiaimless"))
keyboard.on_press_key("F3", lambda _: toggle_feature("visrecoil"))
keyboard.on_press_key("F4", lambda _: toggle_feature("antibhop"))

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

            local_team = pm.read_int(local_player + m_iTeamNum)
            entity_list = pm.read_longlong(client + dwEntityList)

            # --- 1. АНТИБХОП ---
            if features["antibhop"] and keyboard.is_pressed("space"):
                flags = pm.read_uint(local_player + m_fFlags)
                if flags & 1: # Если FL_ONGROUND
                    # Снимаем флаг нахождения на земле
                    pm.write_uint(local_player + m_fFlags, flags & ~1)

            # --- 2. GAMESENSE (Дроп оружия) ---
            weapon_handle = pm.read_longlong(local_player + m_pClippingWeapon)
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
            if features["visrecoil"]:
                punch_x = pm.read_float(local_player + m_aimPunchAngle)
                punch_y = pm.read_float(local_player + m_aimPunchAngle + 4)

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

            # --- 4. ПАРСИНГ ВРАГОВ (Для Антиаимбота/Антиаимлесса) ---
            enemy_spotted = False
            if features["antiaimbot"] or features["antiaimless"]:
                for i in range(1, 64):
                    list_entry = pm.read_longlong(entity_list + ((8 * (i & 0x7FFF)) >> 9) + 16)
                    if not list_entry:
                        continue
                    pawn = pm.read_longlong(list_entry + 120 * (i & 0x1FF))
                    if not pawn or pawn == local_player:
                        continue

                    health = pm.read_int(pawn + m_iHealth)
                    team = pm.read_int(pawn + m_iTeamNum)

                    if health > 0 and team != local_team:
                        enemy_spotted = True
                        break

            if enemy_spotted:
                view_x = pm.read_float(client + dwViewAngles)
                view_y = pm.read_float(client + dwViewAngles + 4)

                if features["antiaimless"]:
                    # Жестко уводим в пол
                    pm.write_float(client + dwViewAngles, 89.0)
                    new_y = view_y + 10.0
                    if new_y > 180.0: new_y -= 360.0
                    pm.write_float(client + dwViewAngles + 4, new_y)

                elif features["antiaimbot"]:
                    # Трясем прицел
                    offset_x = view_x + random.uniform(-15.0, 15.0)
                    offset_y = view_y + random.uniform(-15.0, 15.0)
                    
                    if offset_x > 89.0: offset_x = 89.0
                    if offset_x < -89.0: offset_x = -89.0
                    if offset_y > 180.0: offset_y -= 360.0
                    if offset_y < -180.0: offset_y += 360.0
                    
                    pm.write_float(client + dwViewAngles, offset_x)
                    pm.write_float(client + dwViewAngles + 4, offset_y)

        except Exception:
            # Игнорим ошибки чтения (например, во время загрузки карты)
            pass

if __name__ == "__main__":
    draw_menu()
    threading.Thread(target=neverwin_loop, daemon=True).start()
    keyboard.wait("delete") # Кнопка для полного закрытия софта