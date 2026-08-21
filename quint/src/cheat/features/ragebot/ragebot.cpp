#include <sdk/constants.h>
#include <cheat/config/vars.h>
#include <future>
#include <algorithm>
#include <cheat/features/engine_prediction/engine_prediction.h>
#include <cheat/features/movement/movement.h>
#include <cheat/features/aimbot shared/general_shared.h>
#include <latch>
#include <cheat/menu/tabs/aimbot_tab.h>
#include <cheat/menu/hell_gui/drawlist.h>
#include <sdk/interfaces/engine_cvar.h>
#include <imgui.h>
#include <random>
#include <cmath>

#include "../visuals/overlay_features.h"
#include "shotlogger.h"
#include <math/math types/vector.h>
#include "ragebot.h"

void c_ragebot::set_config() {

	int weapon_rage_type = get_ragebot_weapon_type(g_ctx->m_weapon_def_index);
	if (weapon_rage_type == 10)
		m_bad_weapon = true;

	m_config.m_mindamage = GET_VAR(int, tabs::aimbot::get_min_damage_holder_id(weapon_rage_type));

	if (!g_ctx->m_rage_config_needs_update)
		return;

	m_config.m_enabled = GET_VAR(bool, RAGEBOT_PATH(m_enabled_ragebot));
	m_config.m_autofire = GET_VAR(bool, RAGEBOT_PATH(m_autofire));

	m_bad_weapon = false;


	m_config.m_pointscale = GET_VAR(int, tabs::aimbot::get_pointscale_holder_id(weapon_rage_type));

	m_config.m_hitboxes = GET_VAR(std::vector<std::vector<int>>, RAGEBOT_PATH(m_selected_hitboxes)).at(weapon_rage_type);
	m_config.m_multipointed_hitboxes = GET_VAR(std::vector<std::vector<int>>, RAGEBOT_PATH(m_multipoint_hitboxes)).at(weapon_rage_type);
	
	g_ctx->m_rage_config_needs_update = false;
}

bool c_ragebot::scan_result_t::valid_target() {
	return !m_point.is_zero() && m_record && m_damage > 0 && m_record->is_valid() && m_hitbox;
}
c_hitbox_data* c_ragebot::scan_result_t::resolve_hitbox() {
	if (!m_record || !m_hitbox)
		return nullptr;

	
	for (auto& hb : m_record->m_rage_hitboxes) {
		if (hb.m_hitbox_index == m_hitbox->m_hitbox_index) {
			return &hb;
		}
	}

	return nullptr;
}

c_ragebot::scan_result_t c_ragebot::scan_result_t::compare_players(c_ragebot::scan_result_t& other) {
	if (!this->valid_target())
		return other;
	if (!other.valid_target())
		return *this;

	bool this_meets_threshold = this->m_damage >= this->m_damage_threshold;
	bool other_meets_threshold = other.m_damage >= other.m_damage_threshold;

	if (other_meets_threshold && !this_meets_threshold)
		return other;
	if (this_meets_threshold && !other_meets_threshold)
		return *this;

	c_hitbox_data* this_hb = this->resolve_hitbox();
	c_hitbox_data* other_hb = other.resolve_hitbox();
	const bool this_is_head = this_hb && this_hb->m_hit_group == HITGROUP_HEAD;
	const bool other_is_head = other_hb && other_hb->m_hit_group == HITGROUP_HEAD;

	
	if (this_is_head && !other_is_head)
		return *this;
	if (other_is_head && !this_is_head)
		return other;


	return (this->m_damage > other.m_damage) ? *this : other;
}
force_inline bool c_ragebot::should_stop() {
	static auto weapon_accuracy_nospread = g_interfaces->m_engine_convar->find_by_name("weapon_accuracy_nospread");
	if (weapon_accuracy_nospread->get_bool())
		return false;

	if (!GET_VAR(bool, RAGEBOT_PATH(m_autostop)))
		return false;

	if (g_prediction->is_definitely_on_ground()) {
		if (GET_VAR(std::vector<bool>, RAGEBOT_PATH(m_autostop_modes)).at(e_ragebot_autostop_types::rage_autostop_early))
			return true;
	}
	else {
		if (GET_VAR(std::vector<bool>, RAGEBOT_PATH(m_autostop_modes)).at(e_ragebot_autostop_types::rage_autostop_in_air))
			return true;
	}

	return false;
}
bool c_ragebot::has_max_accuracy() {

	static auto weapon_accuracy_nospread = g_interfaces->m_engine_convar->find_by_name("weapon_accuracy_nospread");
	if (weapon_accuracy_nospread->get_bool())
		return true;

	if (!GET_VAR(bool, RAGEBOT_PATH(m_force_shoot)))
		return false;

	if (!g_prediction->is_definitely_on_ground())
		return false;

	const bool should_scope = g_ctx->m_active_weapon_data->m_WeaponType() == WEAPONTYPE_SNIPER_RIFLE;
	const bool is_scoped = g_ctx->m_local_pawn->m_bIsScoped();

	float min_accuracy = 0.f;

	if (auto movement = g_ctx->m_local_pawn->m_pMovementServices(); movement && (movement->m_bDucking() || movement->m_bDucked())) {
		if (is_scoped)
			min_accuracy = g_ctx->m_active_weapon_data->m_flInaccuracyCrouch().m_types[1];
		else
			min_accuracy = g_ctx->m_active_weapon_data->m_flInaccuracyCrouch().m_types[should_scope ? 1 : 0];
	}
	else {
		if (is_scoped)
			min_accuracy = g_ctx->m_active_weapon_data->m_flInaccuracyStand().m_types[1];
		else
			min_accuracy = g_ctx->m_active_weapon_data->m_flInaccuracyStand().m_types[should_scope ? 1 : 0];
	}

	float current_inaccuracy = g_prediction->m_prediction_data.m_inaccuracy;
	float tolerance = min_accuracy * 1.05f + 0.0001f;

	return current_inaccuracy <= tolerance;
}

bool c_ragebot::hit_chance(int hitchance_value) {
	if (hitchance_value <= 0) return true;
	if (!m_data.m_best_target.valid_target() || !m_data.m_best_target.m_hitbox) return false;

	static auto weapon_accuracy_nospread = g_interfaces->m_engine_convar->find_by_name("weapon_accuracy_nospread");
	if (weapon_accuracy_nospread && weapon_accuracy_nospread->get_bool()) return true;

	vec3_t start = g_ctx->m_shoot_position;
	vec3_t end = m_data.m_best_target.m_point;
	c_hitbox_data* hitbox = m_data.m_best_target.m_hitbox;

	float distance = start.dist_to(end);
	float range = g_ctx->m_active_weapon_data->m_flRange();

	if (distance > range * 1.1f) return false;
	g_ctx->m_active_weapon->update_accuracy_penalty();
	vec3_t direction = end - start;
	vec3_t angles;
	g_math->vector_angles(direction, angles);
	


	float _inaccuracy = g_prediction->m_prediction_data.m_inaccuracy;
	float _spread = g_prediction->m_prediction_data.m_spread;


	if (_inaccuracy < 0.0001f && _spread < 0.0001f) return true;

	const int total_seeds = 256;
	int needed_hits = total_seeds * hitchance_value / 100;
	if (needed_hits <= 0) return true;
	if (needed_hits > total_seeds) needed_hits = total_seeds;

	vec3_t forward, right, up;
	g_math->angle_vectors(angles, &forward, &right, &up);

	static auto calculate_spread_angles = g_modules->m_client.find(xx("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 63 EA")).as<void(__fastcall*)(int16_t, int, int, unsigned int, float, float, float, float*, float*)>();

	int hits = 0;
	float line_len = direction.length();
	if (line_len < 0.001f) return false;

	direction /= line_len;

	for (int seed = 1; seed <= total_seeds; seed++) {
		float spread_x, spread_y;

		calculate_spread_angles(
			g_ctx->m_active_weapon->m_AttributeManager()->m_Item()->m_iItemDefinitionIndex(),
			1,
			(int)g_ctx->m_local_pawn->m_bIsScoped(),
			seed,
			_inaccuracy,
			_spread,
			g_ctx->m_active_weapon->m_flRecoilIndex(),
			&spread_x,
			&spread_y
		);

		vec3_t spread_dir = forward + (right * spread_x) + (up * spread_y);
		float spread_len = spread_dir.length();
		if (spread_len < 0.001f) continue;

		spread_dir /= spread_len;

		vec3_t spread_end = start + (spread_dir * distance);

		if (hitbox->segment_intersects_capsule(start, spread_end)) {
			hits++;
			if (hits >= needed_hits) return true;
		}

		if (hits + (total_seeds - seed) < needed_hits) return false;
	}

	return hits >= needed_hits;
}

bool c_ragebot::is_accurate()
{

	if (has_max_accuracy())		
		return true;

	int hitchance = m_config.m_hitchance;


	int weapon_rage_type = get_ragebot_weapon_type(g_ctx->m_weapon_def_index);

	if (!g_prediction->is_definitely_on_ground() &&
		GET_VAR(bool, RAGEBOT_PATH(m_force_shoot)))
	{
		hitchance = GET_VAR(
			int,
			tabs::aimbot::get_air_hitchance_holder_id(weapon_rage_type)
		);
	}
	else
	{
		hitchance = GET_VAR(
			int,
			tabs::aimbot::get_hitchance_holder_id(weapon_rage_type)
		);

	}

	return hit_chance(hitchance);
}

void c_ragebot::generate_capsule_points(
	c_hitbox_data* hitbox,
	tight_array<vec3_t, 64>& points,
	const vec3_t& start)
{
	if (!hitbox || hitbox->m_radius <= 0.f)
		return;

	float pointscale = m_config.m_pointscale;

	if (pointscale <= 1)
		pointscale = calculate_dynamic_point_scale(
			hitbox->m_center,
			hitbox);

	const float radius =
		hitbox->m_radius * (pointscale / 100.f);

	points.emplace_back(hitbox->m_center);

	vec3_t axis =
		(hitbox->m_maxs - hitbox->m_mins).normalized();

	vec3_t forward =
		(hitbox->m_center - start).normalized();

	vec3_t side =
		axis.cross(forward);

	if (side.length_sqr() < 0.001f)
		side = axis.cross(vec3_t(0.f, 0.f, 1.f));

	side.normalize_place();

	vec3_t up =
		axis.cross(side).normalized();

	constexpr float offsets[] =
	{
		0.15f,
		0.35f,
		0.50f,
		0.65f,
		0.85f
	};

	for (float t : offsets)
	{
		vec3_t center =
			hitbox->m_mins +
			(hitbox->m_maxs - hitbox->m_mins) * t;

		points.emplace_back(center);

		points.emplace_back(center + side * radius);
		points.emplace_back(center - side * radius);

		points.emplace_back(center + up * radius);
		points.emplace_back(center - up * radius);

		points.emplace_back(
			center +
			(side + up).normalized() * radius);

		points.emplace_back(
			center +
			(side - up).normalized() * radius);

		points.emplace_back(
			center +
			(-side + up).normalized() * radius);

		points.emplace_back(
			center +
			(-side - up).normalized() * radius);
	}
}


int c_ragebot::calculate_dynamic_point_scale(vec3_t& point, c_hitbox_data* hitbox) {
	if (!hitbox || !g_ctx->m_active_weapon)
		return m_config.m_pointscale;

	const float spread = g_prediction->m_prediction_data.m_spread;
	const float inaccuracy = g_prediction->m_prediction_data.m_inaccuracy;
	const float total_spread = spread + inaccuracy;
	
	const float distance = point.dist_to(g_ctx->m_shoot_position);


	float new_distance = distance / sinf(DEG2RAD(90.f - RAD2DEG(total_spread)));


	float scale = ((hitbox->m_radius - new_distance * total_spread) + 0.1f) * 100.f;


	scale = std::clamp(scale, 0.f, 95.f);

	return static_cast<int>(scale);
}
void c_ragebot::generate_head_points(
	c_hitbox_data* hitbox,
	tight_array<vec3_t, 64>& points,
	const vec3_t& start)
{
	if (!hitbox)
		return;

	float pointscale = m_config.m_pointscale;
	auto scale = std::clamp(pointscale / 100.0f, 0.0f, 1.0f);
	if (scale <= 0.01f)
		return;



	vec3_t view_dir = start - hitbox->m_center;
	vec3_t front_dir, side_dir;
	vec3_t v_proj = view_dir - hitbox->m_dir * view_dir.dot(hitbox->m_dir);

	if (v_proj.length_sqr() < 1e-8f)
	{
		vec3_t tmp = std::fabsf(hitbox->m_dir.z) < 0.99f ? vec3_t(0, 0, 1) : vec3_t(1, 0, 0);
		side_dir = hitbox->m_dir.cross(tmp).normalized();
		front_dir = side_dir.cross(hitbox->m_dir);
	}
	else
	{
		front_dir = v_proj.normalized();
		side_dir = hitbox->m_dir.cross(front_dir).normalized();
	}

	const float radius = scale * hitbox->m_radius;
	const float bottom_arc = A_HALFPI * radius;
	const float total_len = bottom_arc * 2.0f + hitbox->m_axis_len;

	const vec3_t bottom_center = hitbox->m_center - hitbox->m_dir * hitbox->m_half_height;
	const vec3_t top_center = hitbox->m_center + hitbox->m_dir * hitbox->m_half_height;


	for (int i = 0; i < 64; ++i)
	{
		const auto& p = k_multipoint_params[i];
		vec3_t horiz_dir = front_dir * p.cos_angle + side_dir * p.sin_angle;
		float scaled_path = p.path_pos * total_len;

		vec3_t candidate;
		if (scaled_path < bottom_arc)
		{
			float phi = scaled_path / radius;
			vec3_t offset = horiz_dir * (std::cosf(phi) * radius) - hitbox->m_dir * (std::sinf(phi) * radius);
			candidate = bottom_center + offset;
		}
		else if (scaled_path < bottom_arc + hitbox->m_axis_len)
		{
			float z = scaled_path - bottom_arc;
			vec3_t point_on_axis = bottom_center + hitbox->m_dir * z;
			candidate = point_on_axis + horiz_dir * radius;
		}
		else
		{
			float phi = (scaled_path - (bottom_arc + hitbox->m_axis_len)) / radius;
			vec3_t offset = horiz_dir * (std::cosf(phi) * radius) + hitbox->m_dir * (std::sinf(phi) * radius);
			candidate = top_center + offset;
		}

		points.emplace_back(candidate);
	}
}
void c_ragebot::scan_record(scan_result_t& result, lag_record_t* record, c_penetration::player_context_t* penetration_ctx, const bool& allow_multipoint, vec3_t& start) {
	thread_local tight_array<c_hitbox_data*, 19> tls_per_hitbox_occluders;
	thread_local tight_array<vec3_t, 64> tls_head_points;
	thread_local tight_array<vec3_t, 64> tls_horizontal_capsule_points;
	thread_local tight_array<vec3_t, 4> tls_vertical_capsule_points;

	const int target_health = record->m_pawn->m_iHealth();
	int dmg_threshold = m_config.m_mindamage;
	if (dmg_threshold > target_health)
		dmg_threshold = target_health;
	else if (dmg_threshold > 100)
		dmg_threshold = target_health + m_config.m_mindamage - 100;



	result.m_record = record;
	result.m_penetration_context = penetration_ctx;
	result.m_damage = -1;
	result.m_hitbox = nullptr;
	result.m_damage_threshold = dmg_threshold;
	result.m_start = start;
	result.m_world_dist = record->m_origin.dist_to(start);
	result.m_point_projected_radius = -1.0f;

	auto scan_point = [&](c_hitbox_data* hitbox, vec3_t& point) -> bool {
		if (!hitbox || !penetration_ctx)
			return false;

		static c_handle_bullet_penetration_data pen_data;
		pen_data.m_damage = m_local_context.m_damage;
		pen_data.m_penetration = m_local_context.m_penetration;
		pen_data.m_range_modifier = m_local_context.m_range_mod;
		pen_data.m_pen_count = GET_VAR(bool, RAGEBOT_PATH(m_autowall)) ? 4 : 0;

		int current_damage = 0;
		if (penetration_ctx->fire_bullet(start, point, record->m_pawn, pen_data))
			current_damage = static_cast<int>(pen_data.m_damage);

		if (current_damage <= 0 || current_damage < dmg_threshold)
			return false;

		if (!result.m_hitbox || current_damage > result.m_damage) {
			result.m_point = point;
			result.m_hitbox = hitbox;
			result.m_damage = current_damage;
			result.m_point_projected_radius = hitbox->projected_valid_radius(start, point);
			return (current_damage >= target_health && hitbox->m_hit_group == HITGROUP_HEAD);
		}
		return false;
		};

	tight_array<c_hitbox_data*, 19> sorted_hitboxes;
	for (auto& hb : record->m_rage_hitboxes) {
		sorted_hitboxes.emplace_back(&hb);
	}

	std::sort(sorted_hitboxes.begin(), sorted_hitboxes.end(), [&](c_hitbox_data* a, c_hitbox_data* b) {

		return a->m_radius > b->m_radius;
		});

	bool found_lethal = false;

	for (c_hitbox_data* hitbox_ptr : sorted_hitboxes)
	{
		auto& current_hitbox = *hitbox_ptr;

		if (scan_point(&current_hitbox, current_hitbox.m_center))
		{
			found_lethal = true;

			if (current_hitbox.m_hit_group == HITGROUP_HEAD &&
				result.m_damage >= target_health)
				return;
		}

		if (allow_multipoint && current_hitbox.m_multipoint)
		{
			if (current_hitbox.m_hit_group == HITGROUP_HEAD)
			{
				tls_head_points.clear();

				generate_head_points(
					&current_hitbox,
					tls_head_points,
					start
				);

				for (auto& point : tls_head_points)
				{
					if (scan_point(&current_hitbox, point))
						found_lethal = true;
				}
			}
			else
			{
				tls_horizontal_capsule_points.clear();

				generate_capsule_points(
					&current_hitbox,
					tls_horizontal_capsule_points,
					start
				);

				for (auto& point : tls_horizontal_capsule_points)
				{
					if (scan_point(&current_hitbox, point))
						found_lethal = true;
				}
			}

			if (found_lethal &&
				result.m_damage >= target_health &&
				current_hitbox.m_hit_group != HITGROUP_HEAD &&
				current_hitbox.m_hit_group != HITGROUP_CHEST)
				break;
		}
	}
}


void c_ragebot::scan_player(scan_result_t& result,
	tight_array<lag_record_t, 24>* lagcomp_records,
	c_penetration::player_context_t* pen_ctx)
{
	if (!g_ctx || !lagcomp_records || lagcomp_records->empty())
		return;

	auto& newest = lagcomp_records->newest();
	if (!newest.m_pawn || !newest.m_simulation_time)
		return;

	pen_ctx->fill(newest.m_pawn);

	const float newest_sim = newest.m_simulation_time;

	for (auto& record : *lagcomp_records) {
		scan_result_t cur{};
		scan_record(cur, &record, pen_ctx,
			record.m_simulation_time == newest_sim,
			g_ctx->m_shoot_position);

		result = result.compare_players(cur);

		
		if (!result.valid_target() || !result.m_record || !result.m_record->m_pawn)
			continue;

		if (result.m_hitbox &&
			result.m_hitbox->m_hit_group == HITGROUP_HEAD &&
			result.m_damage >= result.m_record->m_pawn->m_iHealth())
			break;
	}
}
void c_ragebot::select_best_target(tight_array<scan_result_t, 64>& results, bool found_kill)
{
	float best_world = FLT_MAX;

	for (auto& r : results) {
		if (!r.valid_target() || !r.m_record || !r.m_record->m_pawn)
			continue;

		if (found_kill && r.m_damage >= r.m_record->m_pawn->m_iHealth()) {
			m_data.m_best_target = r;
			return;
		}

		if (r.m_world_dist < best_world) {
			best_world = r.m_world_dist;
			m_data.m_best_target = r;
		}
	}
}



void c_ragebot::player_scan()
{
	m_data.m_best_target = {};

	auto& players = g_entity_cache->m_players;
	if (players.empty())
		return;

	float best_world = FLT_MAX;

	for (auto& player : players) {
		if (player.m_lag_records.empty() || player.m_pawn == g_ctx->m_local_pawn)
			continue;

		scan_result_t result{};
		scan_player(result, &player.m_lag_records, &player.m_penetration_context);

		if (!result.valid_target() || !result.m_record || !result.m_record->m_pawn)
			continue;

		if (result.m_damage >= result.m_record->m_pawn->m_iHealth()) {
			m_data.m_best_target = result;
			return;
		}

		if (result.m_world_dist < best_world) {
			best_world = result.m_world_dist;
			m_data.m_best_target = result;
		}
	}
}


bool c_ragebot::zeusbot_can_attack() {
	if (!g_ctx->m_local_pawn || !g_ctx->m_local_controller || !g_ctx->m_cmd)
		return false;

	if (!g_ctx->m_active_weapon || !g_ctx->m_active_weapon_data)
		return false;

	if (g_ctx->m_active_weapon_data->m_WeaponType() != WEAPONTYPE_TASER)
		return false;

	if (g_ctx->m_local_pawn->m_iHealth() <= 0)
		return false;

	if (!g_ctx->m_active_weapon->can_shoot(g_ctx->m_local_controller))
		return false;

	return true;
}

bool c_ragebot::zeusbot_is_enemy_in_range(c_cs_player_pawn* p_enemy, float fl_range) {
	if (!p_enemy || !g_ctx->m_local_pawn)
		return false;

	vec3_t vec_local_pos = g_ctx->m_local_pawn->get_eye_pos();
	vec3_t vec_enemy_pos = p_enemy->get_eye_pos();

	return (vec_enemy_pos - vec_local_pos).length() <= fl_range;
}

c_cs_player_pawn* c_ragebot::zeusbot_get_best_target() {
	if (!g_ctx->m_local_pawn || !g_interfaces->m_entity_system)
		return nullptr;

	c_cs_player_pawn* p_best_target = nullptr;
	float fl_best_distance = FLT_MAX;
	const int i_local_team = g_ctx->m_local_pawn->m_iTeamNum();
	vec3_t vec_local_pos = g_ctx->m_local_pawn->get_eye_pos();

	for (auto& player : g_entity_cache->m_players) {
		if (!player.check_and_update_pawn())
			continue;

		c_cs_player_pawn* p_pawn = player.m_pawn;
		if (!p_pawn)
			continue;

		if (p_pawn == g_ctx->m_local_pawn)
			continue;

		if (p_pawn->m_iTeamNum() == i_local_team)
			continue;

		if (!p_pawn->is_enemy())
			continue;

		auto p_scene_node = p_pawn->m_pGameSceneNode();
		if (!p_scene_node || p_scene_node->m_dormant())
			continue;

		if (p_pawn->m_iHealth() <= 0)
			continue;

		vec3_t vec_enemy_pos = p_pawn->get_eye_pos();
		float fl_distance = (vec_enemy_pos - vec_local_pos).length();

		if (fl_distance > 120.0f)
			continue;

		if (fl_distance < fl_best_distance) {
			fl_best_distance = fl_distance;
			p_best_target = p_pawn;
		}
	}

	return p_best_target;
}

void c_ragebot::zeusbot_run() {
	if (!GET_VAR(bool, RAGEBOT_PATH(m_taser_bot)))
		return;

	if (!zeusbot_can_attack()) {
		m_p_last_target = nullptr;
		return;
	}

	c_cs_player_pawn* p_target = zeusbot_get_best_target();
	if (!p_target) {
		m_p_last_target = nullptr;
		return;
	}

	constexpr float ZEUS_RANGE = 105.0f;

	if (!zeusbot_is_enemy_in_range(p_target, ZEUS_RANGE))
		return;

	vec3_t vec_local_pos = g_ctx->m_local_pawn->get_eye_pos();
	vec3_t vec_target_pos = p_target->get_eye_pos();

	vec3_t ang_to_target = g_math->calculate_angle(vec_local_pos, vec_target_pos);

	auto command = g_ctx->m_cmd;
	if (!command)
		return;

	int idx = command->m_pb.m_input_history_field.size() - 1;
	c_csgo_input_history_entry_pb* p_entry = (idx >= 0) ? command->m_pb.m_input_history_field[idx] : nullptr;

	if (p_entry) {
		c_msg_q_angle* p_angles = p_entry->m_view_angles;
		if (p_angles)
			p_angles->set_q_angle(ang_to_target);

		command->m_pb.set_attack1_history_index(idx);
	}
	else {
		command->m_pb.m_base_cmd->get_view_angles()->set_q_angle(ang_to_target);
		command->m_pb.set_attack1_history_index(-1);
	}

	command->m_buttons.m_value |= IN_ATTACK;
	command->m_buttons.m_value_changed |= IN_ATTACK;

	m_p_last_target = p_target;
}

void c_ragebot::zeusbot_draw_radius() {
	if (!GET_VAR(bool, RAGEBOT_PATH(m_taser_bot)))
		return;

	if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
		return;

	if (!g_ctx->m_local_controller)
		return;

	if (!g_ctx->m_local_controller->m_bPawnIsAlive())
		return;

	if (!g_ctx->m_local_pawn || !g_ctx->m_local_pawn->m_pGameSceneNode())
		return;

	if (g_ctx->m_local_pawn->m_iHealth() <= 0)
		return;

	if (!g_ctx->m_active_weapon || !g_ctx->m_active_weapon_data)
		return;

	if (g_ctx->m_active_weapon_data->m_WeaponType() != WEAPONTYPE_TASER)
		return;

	if (!g_interfaces->m_global_vars)
		return;

	constexpr float ZEUS_RANGE = 105.0f;
	vec3_t local_origin = g_ctx->m_local_pawn->m_pGameSceneNode()->m_vecAbsOrigin();
	vec3_t ground_pos = local_origin;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	hellcolor color = GET_VAR(hellcolor, RAGEBOT_PATH(m_taser_bot_radius_color));

	const int num_circles = 64;
	const float circle_size = 4.0f;
	const float rotation_speed = 0.5f;

	static float rotation_angle = 0.0f;
	rotation_angle = g_interfaces->m_global_vars->m_curtime * rotation_speed;
	if (rotation_angle > A_2PI) {
		rotation_angle = std::fmod(rotation_angle, A_2PI);
	}

	for (int i = 0; i < num_circles; ++i) {
		float base_angle = (float(i) / float(num_circles)) * A_2PI;
		float angle = base_angle + rotation_angle;
		vec3_t world_pos = ground_pos;
		world_pos.x += std::cos(angle) * ZEUS_RANGE;
		world_pos.y += std::sin(angle) * ZEUS_RANGE;

		vec2_t screen_pos;
		if (g_math->world_to_screen(world_pos, screen_pos))
			draw->AddCircleFilled(ImVec2(screen_pos.x, screen_pos.y), circle_size, color, 8);
	}
}





void c_ragebot::run() {

	zeusbot_run();

	if (!m_config.m_enabled || m_bad_weapon)
		return m_data.reset();

	if (!m_fired)
		player_scan();



	if (g_ctx->m_cmd->m_sequence_number == m_data.m_last_scan)
		return;

	m_data.m_last_scan = g_ctx->m_cmd->m_sequence_number;

	if (!g_ctx->m_active_weapon->can_shoot(g_ctx->m_local_controller))
		return m_data.reset();


	m_fired = false;

	if (!m_data.m_best_target.valid_target())
		return m_data.reset();

	m_data.m_accurate = is_accurate();
	m_data.m_wants_stop = should_stop();
	m_data.m_wants_lagcomp_bypass = !g_lagcomp->wants_lag_compensation_on_entity(m_data.m_best_target.m_record);
}

void c_ragebot::handle_attacking() {
	if (!m_data.m_best_target.valid_target())
		return;
	m_data.m_best_target.m_angle = g_math->calculate_angle(g_ctx->m_shoot_position, m_data.m_best_target.m_point);


	if (m_data.m_wants_stop)
		g_movement->stop_movement();

	const bool is_sniper = g_ctx->m_active_weapon_data && g_ctx->m_active_weapon_data->m_WeaponType() == WEAPONTYPE_SNIPER_RIFLE;
	const bool is_scoped = g_ctx->m_local_pawn->m_bIsScoped();

	if (GET_VAR(bool, RAGEBOT_PATH(m_auto_scope)) && is_sniper && !is_scoped) {
		g_ctx->m_cmd->m_buttons.m_value |= IN_ATTACK2;
		g_ctx->m_cmd->m_buttons.m_value_changed |= IN_ATTACK2;
		return;
	}

	if (!m_config.m_autofire || !m_data.m_accurate)
		return;

	auto command = g_ctx->m_cmd;


	bool use_silent = GET_VAR(bool, RAGEBOT_PATH(m_silent_aim));


	int history_size = command->m_pb.m_input_history_field.size();
	for (int i = 0; i < history_size; ++i) {
		c_csgo_input_history_entry_pb* entry = command->m_pb.m_input_history_field[i];
		if (!entry)
			continue;

		c_msg_q_angle* angles = entry->m_view_angles;
		if (angles) {
			angles->set_q_angle(m_data.m_best_target.m_angle);
		}

		
		if (entry->m_sv_interp0) {
			entry->m_sv_interp0->set_src_tick(-1);
			entry->m_sv_interp0->set_dst_tick(-1);
			entry->m_sv_interp0->set_fraction(0.0f);
		}

		if (entry->m_sv_interp1) {
			entry->m_sv_interp1->set_src_tick(-1);
			entry->m_sv_interp1->set_dst_tick(-1);
			entry->m_sv_interp1->set_fraction(0.0f);
		}

		if (entry->m_cl_interp) {
			entry->m_cl_interp->set_fraction(0.0f);
		}
	}


	command->m_buttons.m_value |= IN_ATTACK;
	command->m_buttons.m_value_changed |= IN_ATTACK;
	command->m_buttons.m_value_scroll |= IN_ATTACK;

	if (history_size > 0) {
		command->m_pb.set_attack1_history_index(history_size - 1);
	}



	if (m_data.m_wants_lagcomp_bypass) {
		if (GET_VAR(bool, ANTIAIM_PATH(m_hide_shots))) {
			vec3_t invalid_angle(179.9f, std::remainderf(m_data.m_best_target.m_angle.y + 180.f, 360.f), 0.f);
			command->m_pb.m_base_cmd->get_view_angles()->set_q_angle(invalid_angle);
		}
		else
			command->m_pb.m_base_cmd->get_view_angles()->set_q_angle(m_data.m_best_target.m_angle);
	}
	else {
		if (!use_silent)
			command->m_pb.m_base_cmd->get_view_angles()->set_q_angle(m_data.m_best_target.m_angle);
	}
	if (!use_silent)
		g_interfaces->m_csgo_input->set_view_angle(m_data.m_best_target.m_angle);


	if (m_data.m_best_target.m_record && m_data.m_best_target.m_record->m_pawn && m_data.m_best_target.m_hitbox) {
		c_cs_player_controller* target_controller = g_interfaces->m_entity_system->get_base_entity(m_data.m_best_target.m_record->m_pawn->m_hController().get_entry_index()).as<c_cs_player_controller*>();
		if (target_controller) {
			shotlogger::shot_information_t shot_info{};
			shot_info.m_p_record = m_data.m_best_target.m_record;
			shot_info.m_p_hitbox = m_data.m_best_target.m_hitbox;
			shot_info.m_i_predicted_damage = m_data.m_best_target.m_damage;
			shot_info.m_i_hit_chance = m_config.m_hitchance;
			shot_info.m_v_start = g_ctx->m_shoot_position;
			shot_info.m_v_end = m_data.m_best_target.m_point;
			shot_info.m_fl_time = g_interfaces->m_global_vars->m_curtime;
			shot_info.m_str_player_name = target_controller->m_sSanitizedPlayerName();
			if (g_ctx->m_local_controller) {
				float current_time = g_interfaces->m_global_vars->m_curtime;
				float record_time = m_data.m_best_target.m_record->m_simulation_time;
				float time_diff = current_time - record_time;
				static auto sv_maxunlag = g_interfaces->m_engine_convar->find_by_name("sv_maxunlag");
				float max_unlag = sv_maxunlag ? sv_maxunlag->get_float() : 0.2f;
				if (time_diff > 0.0f && time_diff <= max_unlag) {
					int tick_rate = static_cast<int>(1.0f / interval_per_tick);
					shot_info.m_n_backtrack_tick = static_cast<int>(time_diff * tick_rate);
				}
				else if (time_diff < 0.0f) {
					int tick_rate = static_cast<int>(1.0f / interval_per_tick);
					shot_info.m_n_backtrack_tick = static_cast<int>(time_diff * tick_rate);
				}
				else
					shot_info.m_n_backtrack_tick = 0;
			}
			shotlogger::register_shot(shot_info);
		}
	}
	m_fired = true;
}