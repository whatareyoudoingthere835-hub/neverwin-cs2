#pragma once
#include <core/hooks/modules.h>

#include <sdk/entity/pawn.h>
#include <sdk/entity/controller.h>

class c_entity_system
{
public:

    address_t get_base_entity( int index )
    {
        // Сырой обход списка Source 2 (без сигнатур): listEntry = this + 0x10 + 8*(i>>9),
        // element = listEntry + 0x78*(i&0x1FF). Раскладка неизменна, this = dwEntityList.
        if ( (unsigned int)index > 0x7FFE || (unsigned int)(index >> 9) > 0x3F )
            return { nullptr };

        const std::uintptr_t list_entry = *reinterpret_cast<std::uintptr_t*>(std::uintptr_t( this ) + 0x10 + 8 * ( index >> 9 ));
        if ( !list_entry )
            return { nullptr };

        return { reinterpret_cast<void*>(*reinterpret_cast<std::uintptr_t*>(list_entry + 0x78 * ( index & 0x1FF ))) };
    }

    int get_highest_entity_index()
    {
        return *address_t(this).add(0x2090).as<int*>();
    }

    template <class T>
    T* create_entity_by_class_name(const char* name) 
    {
        using function_t = void* (__fastcall*)(void*, int, const char*, int, int, int, char);
        static function_t get_akll = g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 49 8B F8 44 8B F2")).as<function_t>();
        return reinterpret_cast<T*>(get_akll(this, -1, name, 0, -1, -1, 0));
    }

    c_cs_player_pawn* get_player_pawn(int split_screen_slot)
    {
        // dwLocalPlayerPawn из дампа build 14176.
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_modules->m_client.get());
        return *reinterpret_cast<c_cs_player_pawn**>(base + 0x23AA118);
    }

    c_cs_player_controller* get_player_controller(int split_screen_slot)
    {
        // dwLocalPlayerController из дампа build 14176.
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_modules->m_client.get());
        return *reinterpret_cast<c_cs_player_controller**>(base + 0x2384DB0);
    }
};