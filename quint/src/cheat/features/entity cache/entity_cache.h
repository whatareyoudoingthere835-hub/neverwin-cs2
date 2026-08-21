#pragma once
#include <memory>
#include <vector>
#include <utils/tight_array.h>
#include <sdk/entity/controller.h>
#include <sdk/entity/pawn.h>
#include <cheat/features/lag compensation/lag_compensation.h>
#include <cheat/features/penetration/autowall.h>

void cleanup_player_models( c_cs_player_pawn* pawn );

struct cached_player_t {
    cached_player_t( ) = default;
    cached_player_t( c_cs_player_controller* ent, int idx )
        : m_controller( ent ), m_idx( idx )
    {
    }

    c_cs_player_controller* m_controller = nullptr;
    c_cs_player_pawn* m_pawn = nullptr;
    int m_idx = 0;

    bool check_and_update_pawn();
   
    tight_array<lag_record_t, 24> m_lag_records;
    c_penetration::player_context_t m_penetration_context = {};
};

struct entity_object_t {
    entity_object_t() = default;
    entity_object_t(c_base_entity* ent, int idx)
        : m_pEntity(ent), m_idx(idx) { }
    
    int m_idx = 0;
    c_base_entity* m_pEntity = nullptr;
    bool m_bPredictedGrenade = false;
};

class c_entity_cache {
public:
    tight_array<cached_player_t, 64> m_players;
    tight_array<entity_object_t, 50> m_entity;
    tight_array<entity_object_t, 5> m_c4_entity;
    tight_array<entity_object_t, 20> m_grenade_entity;
    tight_array<entity_object_t, 150> m_weapon_entity;

    cached_player_t& find( c_base_entity* entity );
    void on_add( c_entity_instance* inst, c_base_handle handle );
    void on_remove( c_entity_instance* inst, c_base_handle handle );
};

inline auto g_entity_cache = std::make_unique<c_entity_cache>( );
