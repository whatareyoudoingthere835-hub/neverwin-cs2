
#include "lag_compensation.h"
#include <cheat/features/entity cache/entity_cache.h>
#include <sdk/interfaces/global_variables.h>
#include <sdk/interfaces/engine_cvar.h>
#include <sdk/interfaces/csgo_input.h>
#include <sdk/datatypes/scene_object.h>

#include <context.h>
#include <cheat/features/ragebot/ragebot.h>
#include "timestamp/timestamp.h"
#include <cheat/features/visuals/chams.h>



    int s_last_local_team = 0;
    float s_team_join_time = 0.f;
    constexpr float k_spawn_grace_seconds = 2.f;

    bool is_spawn_grace_active() {
        if (!g_interfaces || !g_interfaces->m_global_vars || !g_ctx || !g_ctx->m_local_pawn)
            return true;



        const int team = g_ctx->m_local_pawn->m_iTeamNum();
        const float curtime = g_interfaces->m_global_vars->m_curtime;

        if (team != s_last_local_team) {
            s_last_local_team = team;
            if (team == 2 || team == 3)
                s_team_join_time = curtime;
        }

        if (team != 2 && team != 3)
            return true;

        return (curtime - s_team_join_time) < k_spawn_grace_seconds;
    }

    __declspec(noinline) bool capture_lag_record_bones(
        c_skeleton_instance* skeleton,
        bone_data_t* out_bones,
        size_t bone_bytes)
    {

        if (!skeleton->m_bone_matrix)
            return false;

        if (skeleton->m_bone_count <= 0 || skeleton->m_bone_count > 128)
            return false;

        skeleton->calc_world_space_bones(0xFFFFF);
        memcpy(out_bones, skeleton->m_bone_matrix, bone_bytes);
        return bone_bytes;

        
    
}

bool lag_record_t::should_skip_interpolation()
{
    if (m_pawn->m_nNoInterpolationTick() == g_interfaces->m_global_vars->m_tick_count)
        return true;

    return false;
}

void lag_record_t::try_adjust_velocity()
{
    if (should_skip_interpolation())
    {
        if (m_pawn->m_bSimulationTimeChanged())
        {
            float sim_time_diff = m_pawn->m_flSimulationTime() - m_pawn->m_flOldSimulationTime();
            if (sim_time_diff > 0.0001f)
            {
                float inv_dt = 1.0f / sim_time_diff;

                vec3_t current_origin = m_game_scene_node->m_vecOrigin();
                vec3_t old_origin = m_pawn->m_vOldOrigin();
                vec3_t velocity = (current_origin - old_origin) * inv_dt;

                m_pawn->set_velocity(&velocity);
            }
        }
    }
}

void store_hitboxes(lag_record_t* record, c_penetration::player_context_t& player_ctx)
{
    if (!record || !record->m_skeleton_instance)
        return;

    c_model* model = record->m_skeleton_instance->m_modelState().m_model_handle;
    if (!model)
        return;

    if (model->m_rendermesh_count <= 0 || !model->m_render_meshes)
        return;

    auto render_meshes = model->m_render_meshes->m_meshes;
    if (!render_meshes)
        return;

    auto hitbox_sets = render_meshes[0].m_hitbox_sets;
    if (!hitbox_sets || hitbox_sets[0].m_hitbox_count <= 0)
        return;

    auto hitbox_arr = hitbox_sets[0].m_hitbox;
    if (!hitbox_arr)
        return;

    auto& bones = record->m_bones;
    int   pointscale = g_ragebot->m_config.m_pointscale;

    static constexpr int bone_indices[19] = { 6, 5, 0, 1, 2, 3, 4, 22, 25, 23, 26, 24, 27, 10, 15, 8, 9, 13, 14 };
    for (int i = 0; i < 19; ++i)
        record->m_all_hitboxes.emplace_back(construct_hitbox_data(hitbox_arr[i], bones[bone_indices[i]], false, pointscale));

    auto& store_hitboxes = record->m_rage_hitboxes;

    int raw_threshold = g_ragebot->m_config.m_mindamage > 100
        ? record->m_pawn->m_iHealth() + g_ragebot->m_config.m_mindamage - 100
        : g_ragebot->m_config.m_mindamage;
    int dmg_threshold = std::min(raw_threshold, record->m_pawn->m_iHealth());

    float weapon_damage = g_ctx->m_active_weapon_data ? g_ctx->m_active_weapon_data->m_nDamage() : 100.f;

    for (int menu_hitbox : g_ragebot->m_config.m_hitboxes) {
        bool is_multipointed = std::find(
            g_ragebot->m_config.m_multipointed_hitboxes.begin(),
            g_ragebot->m_config.m_multipointed_hitboxes.end(),
            menu_hitbox) != g_ragebot->m_config.m_multipointed_hitboxes.end();

        switch (menu_hitbox) {
        case 0:
            if (player_ctx.scale_damage(HITGROUP_HEAD, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[0].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[0]);
            }
            break;
        case 1:
            if (player_ctx.scale_damage(HITGROUP_CHEST, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[6].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[6]);
            }
            break;
        case 2:
            if (player_ctx.scale_damage(HITGROUP_CHEST, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[4].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[4]);
            }
            break;
        case 3:
            if (player_ctx.scale_damage(HITGROUP_STOMACH, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[3].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[3]);
            }
            break;
        case 4:
            if (player_ctx.scale_damage(HITGROUP_STOMACH, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[2].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[2]);
            }
            break;
        case 5:
            if (player_ctx.scale_damage(HITGROUP_LEFTARM, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[16].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[16]);
                record->m_all_hitboxes[18].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[18]);
            }
            break;
        case 6:
            if (player_ctx.scale_damage(HITGROUP_LEFTLEG, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[9].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[9]);
                record->m_all_hitboxes[10].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[10]);
            }
            break;
        case 7:
            if (player_ctx.scale_damage(HITGROUP_LEFTLEG, weapon_damage) >= dmg_threshold) {
                record->m_all_hitboxes[11].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[11]);
                record->m_all_hitboxes[12].m_multipoint = is_multipointed;
                store_hitboxes.emplace_back(record->m_all_hitboxes[12]);
            }
            break;
        }
    }
}

bool lag_record_t::setup(c_cs_player_pawn* pawn)
{
    if (!pawn  || !pawn->is_alive())
        return false;

    m_pawn = pawn;

    m_game_scene_node = pawn->m_pGameSceneNode();
    if (!m_game_scene_node)
        return false;

    m_skeleton_instance = m_game_scene_node->get_skeleton_instace();
    if (!m_skeleton_instance)
        return false;

    if (!m_skeleton_instance->m_bone_matrix)
        return false;

    m_origin = m_game_scene_node->m_vecAbsOrigin();
    m_rotation = m_game_scene_node->m_angAbsRotation();
    m_simulation_time = m_pawn->m_flSimulationTime();

    if (!capture_lag_record_bones(m_skeleton_instance, m_bones, BONE_MATRIX_MEMORY_SIZE))
        return false;

    return true;
}

bool lag_record_t::is_valid()
{
    if (!g_ctx->m_local_controller)
        return false;

    auto network_client = g_interfaces->m_network_client_services->get_network_game_client();
    auto chan_info = g_interfaces->m_engine->get_net_channel_info();
    if (!chan_info || !network_client)
        return false;

    auto fixed_sim_time = timestamp_t{ m_simulation_time };
    auto next_tick = g_ctx->m_local_controller->m_nTickBase() + 1;
    auto tick_count = timestamp_t{ next_tick, 0.f };
    auto net_latency = timestamp_t{ chan_info->get_net_latency() };

    static auto sv_maxunlag = g_interfaces->m_engine_convar->find_by_name("sv_maxunlag");
    auto max_unlag = timestamp_t{ sv_maxunlag->get_float() };

    auto sim_time = tick_count - net_latency;
    auto delta = fixed_sim_time - sim_time;
    delta.normalize();

    if (fixed_sim_time < tick_count - max_unlag)
        return false;

    if (tick_count < fixed_sim_time)
        return false;

    return fabsf(g_interfaces->m_global_vars->m_curtime - m_simulation_time) <= 0.2f;
}
void c_lag_compensation::run()
{
    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;
    if (!g_ctx->m_local_pawn)
        return;
    if (is_spawn_grace_active())
        return;

    g_penetration->update_local_ctx();

    for (auto& ref_player : g_entity_cache->m_players) {
        if (!ref_player.check_and_update_pawn()) {
            ref_player.m_lag_records.clear();
            continue;
        }

        const bool is_local = (ref_player.m_pawn == g_ctx->m_local_pawn);

        if (is_local) {
            const bool should_update = ref_player.m_lag_records.empty() ||
                ref_player.m_pawn->m_flSimulationTime() >
                ref_player.m_lag_records.newest().m_simulation_time;

            if (should_update) {
                lag_record_t new_record = {};
                if (new_record.setup(ref_player.m_pawn)) {
                    new_record.m_visual_only = true;
                    ref_player.m_penetration_context.fill(ref_player.m_pawn);
                    store_hitboxes(&new_record, ref_player.m_penetration_context);
                    ref_player.m_lag_records.push_back(new_record);
                }
            }

       
            while (!ref_player.m_lag_records.empty() &&
                !ref_player.m_lag_records.front().is_valid()) {
                ref_player.m_lag_records.erase(ref_player.m_lag_records.begin());
            }
            continue;
        }

        if (!ref_player.m_pawn->is_enemy()) {
            ref_player.m_lag_records.clear();
            continue;
        }

        if (ref_player.m_pawn->is_alive() && !ref_player.m_pawn->m_bGunGameImmunity()) {
            ref_player.m_penetration_context.fill(ref_player.m_pawn);

            bool should_update = ref_player.m_lag_records.empty() ||
                ref_player.m_pawn->m_flSimulationTime() >
                ref_player.m_lag_records.newest().m_simulation_time;

            if (should_update) {
                lag_record_t new_record = {};
                if (!new_record.setup(ref_player.m_pawn)) {
                    ref_player.m_lag_records.clear();
                    continue;
                }

                store_hitboxes(&new_record, ref_player.m_penetration_context);
                ref_player.m_lag_records.push_back(new_record);

                if (GET_VAR(bool, RAGEBOT_PATH(m_enabled_extrapolation)) && !ref_player.m_lag_records.empty()) {
                    const auto& latest = ref_player.m_lag_records.newest();
                    const lag_record_t* prev = nullptr;

                    if (ref_player.m_lag_records.size() > 1) {
                        prev = &ref_player.m_lag_records[ref_player.m_lag_records.size() - 2];
                    }

                    auto extrap_opt = extrapolate(ref_player.m_pawn, latest, prev);
                    if (extrap_opt.has_value()) {
                        lag_record_t extrap_rec = extrap_opt.value();
                        store_hitboxes(&extrap_rec, ref_player.m_penetration_context);
                        ref_player.m_lag_records.push_back(extrap_rec);
                    }
                }
            }

         
            while (!ref_player.m_lag_records.empty() &&
                !ref_player.m_lag_records.front().is_valid()) {
                ref_player.m_lag_records.erase(ref_player.m_lag_records.begin());
            }
        }
        else {
            ref_player.m_lag_records.clear();
        }
    }
}

void c_lag_compensation::force_input_history()
{
    static auto parse_input_message = g_modules->m_client.find(xx("48 89 5C 24 ? 55 57 41 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 8B 01 48 8B F9")).as<__int64(__fastcall*)(c_cs_input_message*, c_csgo_input_history_entry_pb*, bool, timestamp_t, timestamp_t, c_cs_player_pawn*)>();

    auto& target = g_ragebot->m_data.m_best_target;

    if (!target.m_record || !target.m_record->m_pawn)
        return;

    auto command = g_ctx->m_cmd;
    if (!command)
        return;

    timestamp_t interpolation_timing0 = timestamp_t(target.m_record->m_pawn->get_some_timing(0, 1));
    timestamp_t interpolation_timing1 = timestamp_t(target.m_record->m_pawn->get_some_timing(1, 1));

    c_cs_input_message input_message = {};

    input_message.m_view_angles = target.m_angle;
    input_message.m_player_tick_count = command->m_pb.m_base_cmd->m_client_tick;
    input_message.m_render_tick_count = time_to_ticks(target.m_record->m_simulation_time) + interpolation_timing0.m_tick;
    input_message.m_player_tick_fraction = interpolation_timing0.m_frac;
    input_message.m_render_tick_fraction = interpolation_timing0.m_frac;

    for (int i = 0; i < command->m_pb.m_input_history_field.size(); i++) {
        c_csgo_input_history_entry_pb* entry = command->m_pb.m_input_history_field[i];
        if (!entry)
            continue;

        parse_input_message(&input_message, entry, true, interpolation_timing0, interpolation_timing1, target.m_record->m_pawn);
    }
}

bool c_lag_compensation::wants_lag_compensation_on_entity(lag_record_t* victim)
{
    vec3_t difference = victim->m_origin - g_ctx->m_local_pawn->m_pGameSceneNode()->m_vecOrigin();
    difference.normalize_place();

    vec3_t forward;
    g_ctx->m_base->get_view_angles()->m_ang_value.to_directions(&forward, nullptr, nullptr);

    if (forward.dot(difference) < VALID_LAGCOMP_CONE_COSINE)
        return false;

    return true;
}

void c_lag_compensation::predict_movement(extrapolation_data_t& data, c_cs_player_pawn* skip_pawn)
{
    ///thx https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/combat/impl/extrapolation.cpp
    if (!g_interfaces || !g_interfaces->m_phys2world)
        return;
    static auto sv_gravityconvar = g_interfaces->m_engine_convar->find_by_name("sv_gravity");
    const float sv_gravity = sv_gravityconvar->get_float();;
    const float dt = interval_per_tick;

    if (data.m_flags & FL_ONGROUND) {
        data.m_velocity.z = 0.0f;
    }
    else {
        data.m_velocity.z -= sv_gravity * dt;
    }

    const vec3_t move_end = data.m_origin + data.m_velocity * dt;

    c_ray ray{};
    c_trace_filter filter{ 0x1C3003, skip_pawn, nullptr, 4 };
    c_game_trace trace_result{};

    g_interfaces->m_phys2world->trace_shape(&ray, &data.m_origin, const_cast<vec3_t*>(&move_end), &filter, &trace_result);

    if (trace_result.m_fraction != 1.0f) {
        for (int i = 0; i < 2; ++i) {
            const float dot = data.m_velocity.dot(trace_result.m_hit_normal);
            data.m_velocity -= trace_result.m_hit_normal * dot;

            const float adjust = data.m_velocity.dot(trace_result.m_hit_normal);
            if (adjust < 0.0f) {
                data.m_velocity -= trace_result.m_hit_normal * adjust;
            }

            const float remaining_fraction = 1.0f - trace_result.m_fraction;
            const vec3_t clip_end = trace_result.m_end_pos + data.m_velocity * (dt * remaining_fraction);

            g_interfaces->m_phys2world->trace_shape(&ray, &trace_result.m_end_pos, const_cast<vec3_t*>(&clip_end), &filter, &trace_result);

            if (trace_result.m_fraction == 1.0f)
                break;
        }
    }

    data.m_origin = (trace_result.m_fraction == 1.0f) ? move_end : trace_result.m_end_pos;

    vec3_t ground_end = { data.m_origin.x, data.m_origin.y, data.m_origin.z - 2.0f };
    c_game_trace ground_trace{};
    g_interfaces->m_phys2world->trace_shape(&ray, &data.m_origin, &ground_end, &filter, &ground_trace);

    data.m_flags &= ~FL_ONGROUND;

    if (ground_trace.m_fraction != 1.0f && ground_trace.m_hit_normal.z > 0.7f) {
        data.m_flags |= FL_ONGROUND;
    }
}

std::optional<lag_record_t> c_lag_compensation::extrapolate(c_cs_player_pawn* pawn, const lag_record_t& latest, const lag_record_t* prev)
{
    /// thx https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/combat/impl/extrapolation.cpp
    if (!GET_VAR(bool, RAGEBOT_PATH(m_enabled_extrapolation)))
        return std::nullopt;

    if (!pawn || !g_interfaces || !g_interfaces->m_global_vars)
        return std::nullopt;

    const int server_tick = g_interfaces->m_global_vars->m_tick_count;
    const int record_tick = time_to_ticks(latest.m_simulation_time);
    const int delta_ticks = server_tick - record_tick;

    if (delta_ticks <= 0)
        return std::nullopt;

    const int max_extrap = GET_VAR(int, RAGEBOT_PATH(m_max_extrapolation_ticks));
    const int ticks_to_extrapolate = std::min(delta_ticks, max_extrap);

    if (ticks_to_extrapolate <= 0)
        return std::nullopt;

    vec3_t velocity = pawn->m_vecAbsVelocity();
    const float speed = std::sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);

    if (speed < 0.1f)
        return std::nullopt;

    float direction = 0.0f;
    if (velocity.x != 0.0f || velocity.y != 0.0f) {
        direction = std::atan2f(velocity.y, velocity.x) * (180.0f / 3.14159265f);
    }

    float direction_change = 0.0f;

    if (prev && prev->m_simulation_time > 0.f) {
        const float dt = latest.m_simulation_time - prev->m_simulation_time;
        if (dt > 0.0f) {
            const vec3_t origin_delta = latest.m_origin - prev->m_origin;
            float prev_dir = 0.0f;

            if (origin_delta.x != 0.0f || origin_delta.y != 0.0f) {
                prev_dir = std::atan2f(origin_delta.y, origin_delta.x) * (180.0f / 3.14159265f);
            }

            float angle_diff = direction - prev_dir;
            while (angle_diff > 180.0f) angle_diff -= 360.0f;
            while (angle_diff < -180.0f) angle_diff += 360.0f;

            if (std::fabsf(angle_diff) > 35.0f)
                return std::nullopt;

            direction_change = (angle_diff / dt) * interval_per_tick;
        }
    }

    if (std::fabsf(direction_change) > 6.0f) {
        direction_change = 0.0f;
    }

    vec3_t obb_mins = { -16.0f, -16.0f, 0.0f };
    vec3_t obb_maxs = { 16.0f, 16.0f, 72.0f };
    auto collision = pawn->m_pCollision();
    if (collision) {
        obb_mins = collision->m_vecMins();
        obb_maxs = collision->m_vecMaxs();
    }

    extrapolation_data_t data{};
    data.m_origin = latest.m_origin;
    data.m_velocity = velocity;
    data.m_mins = obb_mins;
    data.m_maxs = obb_maxs;
    data.m_flags = pawn->m_fFlags();
    data.m_sim_time = latest.m_simulation_time;
    data.m_direction = direction;

    for (int i = 0; i < ticks_to_extrapolate; ++i) {
        data.m_direction += direction_change;
        while (data.m_direction > 180.0f) data.m_direction -= 360.0f;
        while (data.m_direction < -180.0f) data.m_direction += 360.0f;

        const float rad = data.m_direction * (3.14159265f / 180.0f);
        const float current_speed = std::sqrtf(data.m_velocity.x * data.m_velocity.x + data.m_velocity.y * data.m_velocity.y);
        data.m_velocity.x = std::cosf(rad) * current_speed;
        data.m_velocity.y = std::sinf(rad) * current_speed;

        data.m_sim_time += interval_per_tick;

        predict_movement(data, pawn);
    }

    const vec3_t origin_delta = data.m_origin - latest.m_origin;
    if (origin_delta.length_sqr() < 0.01f)
        return std::nullopt;

    lag_record_t extrap_record = latest;
    extrap_record.m_origin = data.m_origin;
    extrap_record.m_extrapolated = true;

    for (int i = 0; i < 128; ++i) {
        extrap_record.m_bones[i].m_pos += origin_delta;
    }

    return extrap_record;
}