#include "weapon.h"

#include <utils/memory.h>
#include <core/interfaces/interfaces.h>
#include <sdk/interfaces/engine_cvar.h>
#include <sdk/interfaces/global_variables.h>
#include <sdk/constants.h>
#include <context.h>

int c_econ_item_view::get_custom_paint_kit()
{
	return g_memory->call_virtual<int>(this, 2);
}

c_econ_item_definition* c_econ_item_view::get_econ_item_definition()
{
	return g_memory->call_virtual<c_econ_item_definition*>(this, 15);
}

void c_weapon_base::regen_weapon_skin() {
	using fn_regen_weapon_skin = void(__fastcall*)(void*, bool);
	static fn_regen_weapon_skin regen_weapon_skin_ = g_modules->m_client.find(xx("40 55 53 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 44 0F B6 FA 48 8B D9 BA ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ?")).as<fn_regen_weapon_skin>();

	regen_weapon_skin_(this, false);
}

void c_weapon_base::update_model() {
	using fn_update_model = void(__fastcall*)(__int64, bool);
	static fn_update_model update_model_ = g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 44 0F B6 F2")).as<fn_update_model>();

	update_model_((__int64)((__int64)this + 0x608), true);
}

void c_weapon_base::update_subclass()
{
	using fn_update_subclass = void(__fastcall*)(void*);
	static fn_update_subclass update_class = g_modules->m_client.find(xx("4C 8B DC 53 48 81 EC ? ? ? ? 48 8B 41 10 48 8B D9 8B 50 30 C1 EA 04")).as<fn_update_subclass>();
	update_class(this);
}

void* c_weapon_base::update_composite() {
	return g_memory->call_virtual<void*>(this, 12, true);
}

void* c_weapon_base::update_composite_sec() {
	return g_memory->call_virtual<void*>(this, 105, true);
}

void* c_weapon_base::update_weapon_data() {
	return g_memory->call_virtual<void*>(this, 195u);
}

float speed_ratio_calc(float current_speed_2d, float accurate_speed, float adjusted_full_speed, float zero = 0.0f, float one = 1.0f) {

	if (accurate_speed == adjusted_full_speed) {
		if ((current_speed_2d - adjusted_full_speed) >= 0.0f)
			return one;
		return zero;
	}

	float v6 = current_speed_2d - accurate_speed;
	float v7 = adjusted_full_speed - accurate_speed;
	float v8 = 0.0f;
	float v9 = v6 / v7;

	if (v9 >= 0.0f)
		v8 = fminf(1.0f, v9);

	return ((one - zero) * v8) + zero;
}


float c_weapon_base::get_inaccuracy() {
	using fn_get_inaccuracy_t = float(__fastcall*)(void*, float*, float*);

	float x{}, y{};
	static fn_get_inaccuracy_t fn = g_modules->m_client.find(xx("48 89 5C 24 ? 55 56 57 48 81 EC ? ? ? ? 44 0F 29 84 24")).as<fn_get_inaccuracy_t>();
	return fn(this, &x, &y);
	//return g_memory->call_virtual<float>( this, 411, &unk0, &unk1 );

	// sorry, something also kinda wrong in here.. 
	c_cs_player_pawn* owner = m_hOwnerEntity().get<c_cs_player_pawn>();
	if (!owner)
		return 0.f;

	static auto weapon_accuracy_forcespread = g_interfaces->m_engine_convar->find_by_name("weapon_accuracy_forcespread");
	float force_spread = weapon_accuracy_forcespread->get_float();
	if (force_spread > 0.f)
		return fminf(force_spread, 1.f);

	static auto weapon_accuracy_nospread = g_interfaces->m_engine_convar->find_by_name("weapon_accuracy_nospread");
	if (weapon_accuracy_nospread->get_bool())
		return 0.f;

	float max_speed = get_max_speed();
	float accuracy_penalty = m_fAccuracyPenalty();
	vec3_t velocity = owner->m_vecAbsVelocity();
	float current_speed = velocity.length_2d();
	float move_inaccuracy = get_move_inaccuracy(m_weaponMode());

	bool is_walking = g_ctx->m_local_pawn->m_bIsWalking();
	float speed_ratio = speed_ratio_calc(current_speed, max_speed * 0.34f, max_speed * 0.94999999f);

	float scaled_move_inaccuracy = 0.f;
	if (speed_ratio > 0.f) {
		if (!is_walking)
			speed_ratio = powf(speed_ratio, 0.25f);
		scaled_move_inaccuracy = speed_ratio * move_inaccuracy;
	}

	float total_inaccuracy = accuracy_penalty + scaled_move_inaccuracy;

	if (!owner->m_hGroundEntity().get<c_base_entity>()) {
		static auto weapon_air_spread_scale = g_interfaces->m_engine_convar->find_by_name("weapon_air_spread_scale");
		float air_spread_scale = weapon_air_spread_scale->get_float();

		float inaccuracy_jump_initial = weapon_data()->m_flInaccuracyJumpInitial();
		float inaccuracy_jump_apex = weapon_data()->m_flInaccuracyJumpApex();

		static auto sv_jump_impulse = g_interfaces->m_engine_convar->find_by_name("sv_jump_impulse");
		float jump_impulse = sv_jump_impulse->get_float();
		float jump_impulse_sqrt = jump_impulse >= 0.f ? sqrtf(jump_impulse) : 0.f;

		float vertical_vel_sqrt = velocity.z >= 0.f ? sqrtf(velocity.z) : 0.f;

		float lerp_apex = inaccuracy_jump_apex;
		if ((jump_impulse_sqrt * 0.25f) == jump_impulse_sqrt) {
			if ((vertical_vel_sqrt - jump_impulse_sqrt) >= 0.f)
				lerp_apex = inaccuracy_jump_initial;
		}
		else {
			float ratio = (vertical_vel_sqrt - (jump_impulse_sqrt * 0.25f)) / (jump_impulse_sqrt - (jump_impulse_sqrt * 0.25f));
			lerp_apex = ((inaccuracy_jump_initial - inaccuracy_jump_apex) * ratio) + inaccuracy_jump_apex;
		}

		float air_inaccuracy = fmaxf(0.f, fminf(inaccuracy_jump_initial * 2.f, lerp_apex));
		total_inaccuracy += air_spread_scale * air_inaccuracy;
	}


	float turning_inaccuracy = m_flTurningInaccuracy();
	total_inaccuracy += turning_inaccuracy;

	float strafe_inaccuracy = 0.f; // leaving him as nothing atm, its never enaled but can call a func, i wont rebuild it ig
	//  = get_additional_strafe_inaccuracy( );
	total_inaccuracy += strafe_inaccuracy;

	float rebuild_result = fminf(1.0f, total_inaccuracy);
}

float c_weapon_base::get_spread() {
	static auto fn = g_modules->m_client.find(xx("48 83 EC ? 48 63 91")).as<float(__fastcall*)(c_weapon_base*)>();
	return fn(this);
}

float c_weapon_base::get_max_speed() {
	return g_memory->call_virtual<float>(this, 366);
}

float c_weapon_base::get_ladder_inaccuracy(int mode) {
	return g_memory->call_virtual<float>(this, 347, mode);
}

float c_weapon_base::get_move_inaccuracy(int mode) {
	return g_memory->call_virtual<float>(this, 349, mode);
}

float c_weapon_base::get_cycle_time(int mode) {
	return g_memory->call_virtual<float>(this, 341, mode);
}

float c_weapon_base::get_duck_inaccuracy(int mode) {
	return g_memory->call_virtual<float>(this, 343, mode);
}

float c_weapon_base::get_stand_inaccuracy(int mode) {
	return g_memory->call_virtual<float>(this, 344, mode);
}

float c_weapon_base::get_jump_inaccuracy(int mode) {
	return g_memory->call_virtual<float>(this, 345, mode);
} 

float c_weapon_base::get_weapon_inaccuracy_recovery_time() {
	static auto fn = g_modules->m_client.find(xx("E8 ? ? ? ? F3 0F 10 B7 ? ? ? ? F3 0F 5E F8")).as<float(__fastcall*)(c_weapon_base*)>();
	return fn(this);
}

void c_weapon_base::update_turning_inaccuracy() {
	static auto fn = g_modules->m_client.find(xx("40 56 48 81 EC ? ? ? ? 48 8B F1 BA ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 85 C0 75 0B 48 8B 05 ? ? ? ? 48 8B 40 08 80 38 00 0F 84 ? ? ? ? 48 89 BC 24 ? ? ? ? 48 8D BE ? ? ? ? 0F 29 B4 24 ? ? ? ? 0F 57 F6 0F 2E 37 7A 4B 75 49 0F 2E 77 04 7A 43 75 41 0F 2E 77 08 7A 3B 75")).as<float(__fastcall*)(c_weapon_base*)>();
	fn(this);
}

void c_weapon_base::update_accuracy_penalty() {

	//g_memory->call_virtual<void>( this, 412 );

	static auto fn = g_modules->m_client.find(xx("40 57 41 56 48 83 EC ? 48 8B F9 E8")).as<void(__fastcall*)(c_weapon_base*)>();
	fn(this);


	return;

	// something is completely fucked with the rebuild
	static auto weapon_air_spread_scale = g_interfaces->m_engine_convar->find_by_name("weapon_air_spread_scale");

	if (!g_interfaces->m_global_vars->m_curtime)
		return;

	c_base_entity* owner = m_hOwnerEntity().get<c_base_entity>();
	if (!owner)
		return;

	float curtime = g_interfaces->m_global_vars->m_curtime;
	float inaccuracy = 0.f;

	auto flags = owner->m_fFlags();
	auto move_type = owner->m_nActualMoveType();
	auto weapon_mode = m_weaponMode();

	if (move_type == MOVETYPE_LADDER)
		inaccuracy += (get_ladder_inaccuracy(weapon_mode) + get_ladder_inaccuracy(0));

	if (!(flags & FL_ONGROUND))
		inaccuracy = get_stand_inaccuracy(weapon_mode) + (get_jump_inaccuracy(weapon_mode) * weapon_air_spread_scale->get_float());
	else
		(flags & FL_DUCKING) ? get_duck_inaccuracy(weapon_mode) : get_stand_inaccuracy(weapon_mode);

	if (m_bInReload())
		inaccuracy += weapon_data()->m_flInaccuracyReload().m_types[0];

	if (inaccuracy <= m_fAccuracyPenalty()) {

		static float logarithm_ten = logf(10.f);

		float inaccuracy_recovery_time = get_weapon_inaccuracy_recovery_time();
		float accuracy_penalty = m_fAccuracyPenalty();

		m_fAccuracyPenalty() = expf(-(logarithm_ten / inaccuracy_recovery_time) * 0.015625f) * (accuracy_penalty - inaccuracy) + inaccuracy;
	}
	else {
		m_fAccuracyPenalty() = inaccuracy;
	}

	update_turning_inaccuracy();

	if (m_flRecoilIndex() > 0.f) {

		float cycle_time = get_cycle_time(weapon_mode);
		float last_shot_time = m_fLastShotTime();

		if (curtime > (last_shot_time + cycle_time + 0.015625f)) {
			static float logarithm_ten_doubled = logf(10.0f) * 2.0f;
			const float decay = expf(-0.015625f * logarithm_ten_doubled);

			float new_recoil_index = m_flRecoilIndex() * decay;
			if (new_recoil_index > 0.f && new_recoil_index <= 0.1f)
				new_recoil_index = 0.f;

			m_flRecoilIndex() = new_recoil_index;
		}
	}

	m_flLastAccuracyUpdateTime() = curtime;
}

void c_weapon_base::try_update_accuracy() {
	static float last_time = 0.f;
	if (g_interfaces->m_global_vars->m_curtime != last_time) {
		update_accuracy_penalty();
		last_time = g_interfaces->m_global_vars->m_curtime;
	}
}

bool c_weapon_base::is_at_max_accuracy() {
	return false;
}


c_weapon_base::c_base_gunfire_data c_weapon_base::get_weapon_attack_time() {
	static auto fn = g_modules->m_client.find(xx("48 8B C4 55 53 56 57 41 55 41 56 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 A8 33")).as<void* (__fastcall*)(c_weapon_base*, c_base_gunfire_data*, int)>();
	c_weapon_base::c_base_gunfire_data data = {};
	fn(this, &data, 0);
	return data;
}