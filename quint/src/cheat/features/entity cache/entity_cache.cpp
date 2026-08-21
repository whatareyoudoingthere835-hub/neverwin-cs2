#include "entity_cache.h"
#include <sdk/entity/controller.h>
#include <sdk/entity/pawn.h>
#include <sdk/interfaces/schema_system.h>
#include <core/interfaces/interfaces.h>
#include <cheat/features/lag compensation/lag_compensation.h>
#include <context.h>
#include <cheat/features/penetration/autowall.h>

bool cached_player_t::check_and_update_pawn( ) {
    if ( !m_controller ) return false;
    m_pawn = g_interfaces->m_entity_system
        ->get_base_entity( m_controller->m_hPawn( ).get_entry_index( ) )
        .as<c_cs_player_pawn*>( );
    return (m_pawn != nullptr);
}

cached_player_t& c_entity_cache::find( c_base_entity* entity ) {
    for ( auto& player : m_players ) {
        if ( player.m_pawn == entity )
            return player;
    }
    return m_players.front( );
}

void c_entity_cache::on_add( c_entity_instance* inst, c_base_handle handle ) {
    if ( !inst )
        return;
        
    const int idx = handle.get_entry_index( );
    if ( idx < 0 ) return;

    auto* ent = reinterpret_cast<c_base_entity*>(inst);
    if ( !ent || handle.get_entry_index( ) > 0x3FFF )
        return;
        
    if ( !ent->get_handle().is_valid() || ent->get_handle( ) != handle )
        return;

    auto binding = ent->get_class_binding_base( );
    if ( !binding ) return;

    const auto hashed = fnv_hash( binding->get_name( ) );
    if ( hashed == fnv_hash( xx( "CCSPlayerController" ) ) ) {
        if ( m_players.size( ) < m_players.capacity( ) ) {
            m_players.push_back(
                cached_player_t( static_cast<c_cs_player_controller*>(ent), idx )
            );
        }
    }
    else if (hashed == fnv_hash(xx("C_Inferno"))) {
        m_entity.push_back(entity_object_t(static_cast<c_base_entity*>(ent), idx));
    }
    else if (hashed == fnv_hash(xx("C_PlantedC4"))) {
        m_c4_entity.push_back(entity_object_t(static_cast<c_base_entity*>(ent), idx));
    }
    else if (hashed == fnv_hash(xx("C_HEGrenadeProjectile")) ||
        hashed == fnv_hash(xx("C_FlashbangProjectile")) ||
        hashed == fnv_hash(xx("C_SmokeGrenadeProjectile")) ||
        hashed == fnv_hash(xx("C_DecoyProjectile")) ||
        hashed == fnv_hash(xx("C_MolotovProjectile"))) {
        m_grenade_entity.push_back(entity_object_t(static_cast<c_base_entity*>(ent), idx));
    }
    else if (ent->is_weapon2()) {
        m_weapon_entity.push_back(entity_object_t(static_cast<c_base_entity*>(ent), idx));
    }
}

void c_entity_cache::on_remove( c_entity_instance* inst, c_base_handle handle ) {
    if ( !inst )
        return;
        
    const int idx = handle.get_entry_index( );
    if ( idx < 0 ) return;

    auto* ent = reinterpret_cast<c_base_entity*>(inst);
    if ( !ent || handle.get_entry_index( ) > 0x3FFF )
        return;
        
    if ( !ent->get_handle().is_valid() || ent->get_handle( ) != handle )
        return;

    auto binding = ent->get_class_binding_base( );
    if ( !binding ) return;

    const auto hashed = fnv_hash( binding->get_name( ) );
    if ( hashed == fnv_hash( xx( "CCSPlayerController" ) ) ) {
        for ( size_t i = 0; i < m_players.size( ); ++i ) {
            if ( m_players[i].m_idx == idx ) {
                if ( m_players[i].m_pawn ) {
                    cleanup_player_models( m_players[i].m_pawn );
                }
                m_players[i].m_lag_records.clear( );
                m_players.erase_unordered( i );
                break;
            }
        }
    }
    else if (hashed == fnv_hash(xx("C_Inferno"))) {
        for (size_t i = 0; i < m_entity.size(); ++i) {
            if (m_entity[i].m_idx == idx) {
                m_entity.erase_unordered(i);
                break;
            }
        }
    }
    else if (hashed == fnv_hash(xx("C_PlantedC4"))) {
        for (size_t i = 0; i < m_c4_entity.size(); ++i) {
            if (m_c4_entity[i].m_idx == idx) {
                m_c4_entity.erase_unordered(i);
                break;
            }
        }
    }
    else if (hashed == fnv_hash(xx("C_HEGrenadeProjectile")) ||
        hashed == fnv_hash(xx("C_FlashbangProjectile")) ||
        hashed == fnv_hash(xx("C_SmokeGrenadeProjectile")) ||
        hashed == fnv_hash(xx("C_DecoyProjectile")) ||
        hashed == fnv_hash(xx("C_MolotovProjectile"))) {
        for (size_t i = 0; i < m_grenade_entity.size(); ++i) {
            if (m_grenade_entity[i].m_idx == idx) {
                m_grenade_entity.erase_unordered(i);
                break;
            }
        }
    }
    else if (ent->is_weapon2()) {
        for (size_t i = 0; i < m_weapon_entity.size(); ++i) {
            if (m_weapon_entity[i].m_idx == idx) {
                m_weapon_entity.erase_unordered(i);
                break;
            }
        }
    }
}
