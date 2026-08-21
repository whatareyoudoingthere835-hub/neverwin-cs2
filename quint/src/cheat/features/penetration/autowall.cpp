#include "autowall.h"

#include <sdk/constants.h>
#include <core/interfaces/interfaces.h>
#include <sdk/interfaces/engine_cvar.h>
#include <context.h>

void c_penetration::update_local_ctx() {

    auto ptr = g_ctx->m_active_weapon_data;
    if (!ptr)
        return;

    m_local_context.m_armor_ratio = ptr->m_flArmorRatio();
    m_local_context.m_headshot_multiplier = ptr->m_flHeadshotMultiplier();
    m_local_context.m_damage = ptr->m_nDamage();
    m_local_context.m_penetration = ptr->m_flPenetration();
    m_local_context.m_range_mod = ptr->m_flRangeModifier();
    m_local_context.m_local_team = g_ctx->m_local_pawn->m_iTeamNum();
}

void c_penetration::player_context_t::fill(c_cs_player_pawn* pawn) {
    static auto mp_damage_scale_ct_head = g_interfaces->m_engine_convar->find_by_name("mp_damage_scale_ct_head");
    static auto mp_damage_scale_t_head = g_interfaces->m_engine_convar->find_by_name("mp_damage_scale_t_head");
    static auto mp_damage_scale_ct_body = g_interfaces->m_engine_convar->find_by_name("mp_damage_scale_ct_body");
    static auto mp_damage_scale_t_body = g_interfaces->m_engine_convar->find_by_name("mp_damage_scale_t_body");

    m_pawn = pawn;
    m_skeleton = pawn->m_pGameSceneNode()->get_skeleton_instace();

    const bool is_ct = pawn->m_iTeamNum() == 3;
    const bool is_t = pawn->m_iTeamNum() == 2;

    m_head_scale = is_ct ? mp_damage_scale_ct_head->get_float() : mp_damage_scale_t_head->get_float();
    m_body_scale = is_ct ? mp_damage_scale_ct_body->get_float() : mp_damage_scale_t_body->get_float();

    m_armor_value = static_cast<float>(pawn->m_ArmorValue());

    auto item_service = pawn->m_pItemServices();

    m_has_heavy_armor = false;
    m_has_helmet = item_service->m_bHasHelmet();

    if (m_has_heavy_armor)
        m_head_scale *= 0.5f;

    m_head_scale *= m_local_context.m_headshot_multiplier;
    m_stomach_scale = 1.25f * m_body_scale;
    m_legs_scale = 0.75f * m_body_scale;

    m_armor_ratio = m_local_context.m_armor_ratio * 0.5f;
    m_armor_bonus = 0.5f;
    m_heavy_armor_bonus = 1.0f;

    if (m_has_heavy_armor) {
        m_armor_ratio *= 0.20f;
        m_armor_bonus = 0.33f;
        m_heavy_armor_bonus = 0.25f;
    }

    m_hitgroup_scale[HITGROUP_HEAD] = m_head_scale;
    m_hitgroup_scale[HITGROUP_CHEST] = m_body_scale;
    m_hitgroup_scale[HITGROUP_LEFTARM] = m_body_scale;
    m_hitgroup_scale[HITGROUP_RIGHTARM] = m_body_scale;
    m_hitgroup_scale[HITGROUP_NECK] = m_body_scale;
    m_hitgroup_scale[HITGROUP_STOMACH] = m_stomach_scale;
    m_hitgroup_scale[HITGROUP_LEFTLEG] = m_legs_scale;
    m_hitgroup_scale[HITGROUP_RIGHTLEG] = m_legs_scale;

    for (int i = 0; i < 8; ++i)
        m_hitgroup_has_armor[i] = false;

    if (m_armor_value > 0.f) {
        m_hitgroup_has_armor[HITGROUP_CHEST] = true;
        m_hitgroup_has_armor[HITGROUP_LEFTARM] = true;
        m_hitgroup_has_armor[HITGROUP_RIGHTARM] = true;
        m_hitgroup_has_armor[HITGROUP_NECK] = true;
        m_hitgroup_has_armor[HITGROUP_STOMACH] = true;
        m_hitgroup_has_armor[HITGROUP_HEAD] = m_has_helmet;
    }
}

int c_penetration::player_context_t::scale_damage(const int& hitgroup, float damage) {
    float original_damage = damage;

    damage *= m_hitgroup_scale[hitgroup];

    if (!m_hitgroup_has_armor[hitgroup])
        return static_cast<int>(damage);

    float damage_to_health = damage * m_armor_ratio;
    float damage_to_armor = (damage - damage_to_health) * (m_heavy_armor_bonus * m_armor_bonus);

    if (damage_to_armor > m_armor_value)
        damage_to_health = damage - m_armor_value / m_armor_bonus;

    return static_cast<int>(damage_to_health);
}

void c_penetration::trace_calls_t::create_trace(c_trace_data* trace_data, vec3_t start, vec3_t delta, c_trace_filter* filter) {
    using fn_create_trace = void(__fastcall*) (c_trace_data*, vec3_t, vec3_t, c_trace_filter*, int, bool);
    static fn_create_trace fn = g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? F2 0F 10 02")).as<fn_create_trace>();
   fn(trace_data, start, delta, filter, 4, true);
}

uint64_t c_penetration::trace_calls_t::damage_to_point(c_trace_data* trace_data, float damage, float penetration, float range_modifier, int team_num) {
    using fn_damage_to_point = uint64_t(__fastcall*)(c_trace_data*, float, float, float, int, int, void*);
    static fn_damage_to_point fn = g_modules->m_client.find(xx("40 53 57 41 56 48 83 EC ? 8B 84 24")).as<fn_damage_to_point>();
    return fn(trace_data, damage, penetration, range_modifier, 4, team_num, nullptr);
}

bool c_penetration::player_context_t::fire_bullet(vec3_t start, vec3_t& end, c_cs_player_pawn* target, c_handle_bullet_penetration_data& data) {
    vec3_t direction = end - start;
    vec3_t shoot_angles;
    g_math->vector_angles(direction, shoot_angles);

    c_trace_data trace_data;
    g_interfaces->m_phys2world->initialize_trace_data(&trace_data);

    float spread = g_ctx->m_active_weapon->get_spread();

    vec3_t forward, right, up;
    g_math->angle_vectors(shoot_angles, &forward, &right, &up);

    vec3_t spread_dir = forward - (right * spread) + (up * spread);
    spread_dir.normalize();

    vec3_t delta = spread_dir * g_ctx->m_active_weapon_data->m_flRange();

    trace_data.m_start = start;
    trace_data.m_end = start + delta;

    c_trace_filter filter;
    g_interfaces->m_phys2world->initialize_trace_filter(&filter, g_ctx->m_local_pawn, 0x1C300B, 3, 15);
    filter.m_ptr4 |= 2u;
    filter.m_ptr[0] |= 0x4000000000uLL;

    g_penetration->m_trace_calls.create_trace(&trace_data, start, delta, &filter);
    g_penetration->m_trace_calls.damage_to_point(&trace_data, g_ctx->m_active_weapon_data->m_nDamage(), g_ctx->m_active_weapon_data->m_flPenetration(), g_ctx->m_active_weapon_data->m_flRangeModifier(), g_ctx->m_local_controller->m_iTeamNum());

    for (int i = 0; i < trace_data.m_surfaces_count; i++) {
        c_trace_info& info = trace_data.m_trace_info[i];

        c_game_trace trace;
        void* segment_ptr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(trace_data.m_trace_segments_ptr) +
            0x38 * (info.m_handle.to_int() & 0x7FFF)
            );

        g_interfaces->m_phys2world->setup_game_trace_info(
            &trace_data,
            &trace,
            info.m_distance,
            reinterpret_cast<c_segment_holder*>(segment_ptr)
        );

     
        if (trace.m_ent && trace.m_ent == target) {
            data.m_damage = scale_damage(trace.m_hitbox ? trace.m_hitbox->m_hit_group : 0, info.m_damage);
            return true;
        }
    }

    return false;
}