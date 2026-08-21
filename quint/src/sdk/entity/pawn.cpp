#include "pawn.h"
#include <utils/memory.h>
#include <core/interfaces/interfaces.h>
#include <sdk/interfaces/engine_cvar.h>
#include <sdk/interfaces/global_variables.h>
#include <sdk/constants.h>
#include <context.h>
#include <cheat/features/engine_prediction/engine_prediction.h>

constexpr int COLLISION_GROUP_PLAYER_MOVEMENT = 8;

vec3_t c_player_weapon_services::weapon_shoot_position() {
	vec3_t ret = {};
	g_memory->call_virtual<void>(this, 29, &ret);
	return ret;
}

c_contents* c_cs_player_pawn::get_contents() {
	return g_memory->call_virtual<c_contents*>(this, 63);
}

vec3_t c_cs_player_pawn::get_eye_pos()
{
	vec3_t ret = {};
	g_memory->call_virtual<void>(this, 170, &ret);
	return ret;
}
void c_player_movement_services::run_command(c_user_cmd* cmd)
{
	g_memory->call_virtual<void>(this, 32, cmd);
}
vec3_t c_cs_player_pawn::get_shoot_pos() {
	return m_pGameSceneNode()->m_vecAbsOrigin() + m_vecViewOffset();
}

bool c_cs_player_pawn::is_alive() {
	return m_iHealth() > 0 && m_lifeState() == LIFE_ALIVE;
}
vec3_t c_cs_player_pawn::get_weapon_recoil()
{

	using fn_t = void(__fastcall*)(void*, vec3_t*, bool);
	static fn_t fn = g_modules->m_client.find(
		xx("E8 ? ? ? ? 4C 8B C0 48 8D 55 ? 48 8B CB E8 ? ? ? ? 48 8D 0D")
	).absolute(1, 0).as<fn_t>();

	vec3_t punch = {};
	if (fn)
		fn(this, &punch, true);
	return punch;
}
bool c_cs_player_pawn::is_enemy() {
	c_cs_player_pawn* local_pawn = g_ctx->m_local_pawn;

	if (!local_pawn)
		return true;

	if (this == local_pawn)
		return false;

	return this->m_iTeamNum() != local_pawn->m_iTeamNum();
}

c_weapon_base* c_cs_player_pawn::get_active_weapon()
{
	const auto player_weapon_services = m_pWeaponServices();
	if (!player_weapon_services)
		return nullptr;

	auto entity = player_weapon_services->m_hActiveWeapon().get<c_weapon_base >();
	if (!entity || !entity->is_weapon())
		return nullptr;

	return entity;
}

uint32_t c_cs_player_pawn::get_collision_owner_index()
{
	const auto player_collision = m_pCollision();
	if (player_collision && !(player_collision->m_usSolidFlags() & 4))
		return m_hOwnerEntity().get_entry_index();

	return -1;
}
bool c_cs_player_pawn::has_armor(int hitgroup)
{
	if (hitgroup == HITGROUP_HEAD)
		return m_pItemServices()->m_bHasHelmet();

	return m_ArmorValue();
}

bool c_cs_player_pawn::IsArmored(int hitgroup) {
	if (hitgroup == HITGROUP_HEAD) {
		if (!m_pItemServices())
			return false;

		return m_pItemServices()->m_bHasHelmet();
	}
	else
		return m_ArmorValue() > 0;
}

void c_cs_player_pawn::set_body_group(int first, int second)
{
	using fn_set_body_group = void(__fastcall*)(void*, int, unsigned int);
	static fn_set_body_group set_body_group_ = g_modules->m_client.find(xx("85 D2 0F 88 ? ? ? ? 53 55")).as<fn_set_body_group>();
	set_body_group_(this, first, second);
}

void c_cs_player_pawn::run_pre_think()
{
	g_memory->call_virtual<void>(this, 322);
}

void c_cs_player_pawn::physics_run_think()
{
	using fn_physics_run_think = void(__fastcall*)(void*);
	static fn_physics_run_think fn = g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 8B 81 ? ? ? ? 48 8B F9")).as<fn_physics_run_think>();
	fn(this);
}

void c_cs_player_pawn::run_post_think()
{
	g_memory->call_virtual<void>(this, 333);
}

void c_player_movement_services::set_prediction_command(c_user_cmd* cmd)
{
	using fn_set_prediction_command = void(__fastcall*)(c_player_movement_services*, c_user_cmd*);
	static fn_set_prediction_command reset_fn = g_modules->m_client.find(
		xx("48 89 5C 24 ? 57 48 83 EC ? 48 8B DA E8 ? ? ? ? 48 8B F8 48 85 C0 74")
	).as<fn_set_prediction_command>();

	reset_fn(this,cmd);
}

bool c_player_movement_services::should_use_viewangles_for_subtick(c_user_cmd* cmd, c_move_data* data)
{
	return g_memory->call_virtual<bool>(this, 33, cmd, data);
}

void c_player_movement_services::trace_player_bbox(c_move_data* move_data, vec3_t* progression, c_game_trace* trace)
{
	using fn_trace_player_bbox = void(__fastcall*)(void*, c_move_data*, vec3_t*, c_game_trace*);
	static fn_trace_player_bbox fn = g_modules->m_client.find(xx("48 8B C4 4C 89 48 ? 4C 89 40 ? 48 89 50 ? 48 89 48 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 45 33 E4")).as<fn_trace_player_bbox>();
	fn(this, move_data, progression, trace);
}

void c_player_movement_services::get_player_collision_bound(bbox_collision_t* bounds)
{
	using fn_get_player_collision_bound = void* (__fastcall*)();
	static fn_get_player_collision_bound fn = g_modules->m_client.find(xx("48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 FF A0 ? ? ? ? 48 8D 05")).as<fn_get_player_collision_bound>();

	char* bbox = { };

	if (this->m_bDucked())
	{
		bbox = ((char*)fn() + 36);
	}
	else
	{
		bbox = (char*)fn() + 12;
	}

	bounds = (bbox_collision_t*)bbox;
}


void c_player_movement_services::reset_prediction_command() {
	using fn_reset_prediction_command = void(__fastcall*)(c_player_movement_services*);
	static fn_reset_prediction_command reset_fn = g_modules->m_client.find(
		xx("48 83 EC ? B9 ? ? ? ? E8 ? ? ? ? 48 C7 05")
	).as<fn_reset_prediction_command>();

	reset_fn(this);
}



void c_player_movement_services::force_buttons_down(__int64 a2)
{
	using fn_force_buttons_down = void* (__fastcall*)(void*, __int64);
	static fn_force_buttons_down force_buttons_down_ = g_modules->m_client.find(xx("40 53 56 41 56 48 81 EC ? ? ? ? 4C 8B 41")).as<fn_force_buttons_down>();

	force_buttons_down_(this, a2);
}

void c_player_movement_services::setup_movement_moves(c_move_data* move_data)
{
	using fn_setup_movement_moves = void* (__fastcall*)(void*, c_move_data*);
	static fn_setup_movement_moves setup_movement_moves_ = g_modules->m_client.find(xx("48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? 44 0F B6 C0")).as<fn_setup_movement_moves>();

	setup_movement_moves_(this, move_data);
}

void c_player_movement_services::finish_move(c_user_cmd* cmd, c_move_data* move_data)
{
	g_memory->call_virtual<void>(this, 34, cmd, move_data);
}

void c_player_movement_services::post_think() {
	g_memory->call_virtual<void>(this, 5);
}

void c_player_movement_services::quantize_movement(c_move_data* move_data)
{
	static auto sv_quantize_movement_input = g_interfaces->m_engine_convar->find_by_name("sv_quantize_movement_input");

	move_data->game_code_moved_player = (this->m_pawn()->m_fFlags() & FL_ONGROUND) == 0;

	if (sv_quantize_movement_input->get_bool())
	{
		move_data->forward_move = std::roundf(move_data->forward_move);
		move_data->side_move = std::roundf(move_data->side_move);
		move_data->up_move = std::roundf(move_data->up_move);
	}
}

void c_player_movement_services::process_movement(c_move_data* move_data)
{
	g_memory->call_virtual<void>(this, 26, move_data);
}

__int64 c_player_movement_services::process_movement_cmd(c_user_cmd* cmd) {
	using fn = __int64(__fastcall*)(c_player_movement_services*, c_user_cmd*);
	static fn x = g_modules->m_client.find(xx("E8 ? ? ? ? 48 8B 06 48 8B CE FF 90 ? ? ? ? 48 85 DB")).absolute(1, 0).as<fn>();
	return x(this, cmd);
}

void c_player_movement_services::calcualte_jump_height(c_move_data* move_data)
{
	if (this->m_pawn()->m_fFlags() & FL_ONGROUND)
		this->m_flMaxJumpHeightThisJump() = std::fmaxf(move_data->abs_origin.z - this->m_flHeightAtJumpStart(), 0.0f);

	if (this->m_nButtonDownMaskPrev() != this->m_button_state().m_value)
		this->m_nButtonDownMaskPrev() = this->m_button_state().m_value;
}

void c_player_movement_services::process_impacts_attack(c_move_data* move_data)
{
	g_memory->call_virtual<void>(this, 35, move_data);
}

void c_player_movement_services::process_impacts(c_user_cmd* cmd, c_move_data* move_data)
{
	using fn_process_impacts = void* (__fastcall*)(void*, c_user_cmd*, c_move_data*);
	static fn_process_impacts process_impacts_ = g_modules->m_client.find(xx("48 8B C4 53 56 41 55")).as<fn_process_impacts>();

	process_impacts_(this, cmd, move_data);
}

void c_player_movement_services::setup_subtick(c_user_cmd* cmd, c_move_data* move_data)
{
	using fn_setup_subtick = void* (__fastcall*)(void*, c_user_cmd*, c_move_data*, bool);
	static fn_setup_subtick setup_subtick_ = g_modules->m_client.find(xx("48 8B C4 48 89 50 ? 48 89 48 ? 53 57")).as<fn_setup_subtick>();

	setup_subtick_(this, cmd, move_data, 0);
}

void c_player_movement_services::prepare_movement(c_move_data* move_data) {
	g_memory->call_virtual<void>(this, 31, move_data);
}

void c_player_movement_services::setup_move(c_user_cmd* cmd, c_move_data* move_data)
{
	g_memory->call_virtual<void>(this, 30, cmd, move_data);
}

void c_player_movement_services::update_button_states(c_user_cmd* cmd)
{
	g_memory->call_virtual<void>(this, 24, cmd);
}

void c_player_movement_services::check_moving_ground(float frame_time)
{
	g_memory->call_virtual<void>(this, 39, frame_time);
}

c_move_data* c_player_movement_services::reset_move_data()
{
	return g_memory->call_virtual<c_move_data*>(this, 29);
}

float c_cs_player_pawn::get_some_timing(int idx0, int idx1) {
	static auto fn = g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 63 D8 48 8B F1")).as<float(__fastcall*)(c_cs_player_pawn*, int, int)>();
	return fn(this, idx0, idx1);
}

void c_cs_player_pawn::set_velocity(vec3_t* velocity)
{
	static auto fn = g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B EA")).as<void(__fastcall*)(c_cs_player_pawn*, vec3_t*)>();
	fn(this, velocity);
}



c_hud_model_arms* c_cs_player_pawn::get_view_arms()
{

	c_hud_model_arms* m_hud_model = (c_hud_model_arms*)this->m_hud_model_arms().get();
	if (!m_hud_model)
		return nullptr;

	return m_hud_model;
}

std::vector<c_hud_model_weapon*> c_cs_player_pawn::get_view_models()
{
	c_hud_model_arms* pViewArms = get_view_arms(); // m_hHudModelArms
	if (!pViewArms)
		return {};

	c_game_scene_node* pGameSceneNode = pViewArms->m_pGameSceneNode();
	if (!pGameSceneNode) {
		return {};
	}

	std::vector<c_hud_model_weapon*> vecViewModels = {};
	for (c_game_scene_node* pChild = pGameSceneNode->m_child(); pChild; pChild = pChild->m_sibling()) {
		c_base_entity* pOwner = reinterpret_cast<c_base_entity*>(pChild->m_owner());
		if (!pOwner) {
			continue;
		}

		if (pOwner->get_class_name() == (("C_CS2HudModelWeapon")) != std::string::npos) { // pOwner->GetClassName() == "C_CS2HudModelWeapon"
			vecViewModels.push_back(reinterpret_cast<c_hud_model_weapon*>(pOwner));
		}
	}

	return vecViewModels;
}

c_hud_model_weapon* c_cs_player_pawn::get_view_model_brutforce() {

	c_base_player_weapon* pWeapon = this->get_active_weapon(); // CPlayer_WeaponServices -> m_hActiveWeapon
	if (!pWeapon) {
		return nullptr;
	}

	std::vector<c_hud_model_weapon*> vecViewModels = get_view_models();
	if (vecViewModels.empty()) {
		return nullptr;
	}

	for (c_hud_model_weapon* pViewModel : vecViewModels) {
		c_base_entity* pOwner = (c_base_entity*)pViewModel->m_hOwnerEntity().get(); // m_hOwnerEntity
		if (!pOwner) {
			continue;
		}

		if (pOwner == pWeapon) {
			return pViewModel;
		}
	}

	return nullptr;
}
