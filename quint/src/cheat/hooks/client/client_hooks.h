#pragma once

#include <includes.h>
#include <core/hooks/hooks.h>
#include <core/hooks/quint_hook.h>
#include <core/interfaces/interfaces.h>
#include <math/math types/view_matrix.h>
#include <sdk/interfaces/client_mode_cs_normal.h>

class c_client_hooks {
public:
	static bool mouse_input_enabled( void* a1 );
	static void* is_relative_mouse_mode( void* input_system, bool active );
	static void create_move( c_csgo_input* input, int slot, bool active );
	static std::uintptr_t __fastcall setup_map_info(std::uintptr_t map_info, void* unk);
	
	static float get_world_fov( void* rcx );
	static void* calc_viewmodel(float* unk, float* offsets, float* fov);
	static __int64* level_init( void* client_mode_shared, const char* new_map );
	static void level_shutdown( void* client_mode_shared );

	static __int64 add_entity( void* thisptr, c_entity_instance* inst, c_base_handle handle );
	static __int64 remove_entity( void* thisptr, c_entity_instance* inst, c_base_handle handle );
	static bool anti_tamper(void* Src, int a2, __int64 a3, int a4);
	static void get_matrix_for_view( void* ecx, void* setup, void* world_to_view, void* view_to_projection, v_matrix* world_to_projection, void* world_to_pixels );
	static void override_view( void* client_mode_cs_normal, c_view_setup* view_setup );
	//static char cmsg_shoot_info( void* rcx , char a2);
	static void frame_stage_notify( void* source2_client, int stage );
	static void handle_camera_angles( c_csgo_input* input, int a2 );
	static void draw_scope( __int64 a1, __int64 a2 );
	static bool draw_overhead( c_cs_player_pawn* pawn );
	static void* smoke_volume_scene_object_drawarray( void* a1, void* a2, int a3, int a4, void* a5, void* a6, void* a7, void* a8, void* a9, void* a10 );
	static void* draw_flash_overlay( __int64 a1, int a2, __int64* a3, __int64 a4, float a5[4] );
	static void* first_person_legs( void* a1, void* a2, void* a3, void* a4, void* a5 );
	static void handle_glow(c_glow_property* glow_property, float* color );
	static bool m_is_glow(c_glow_property* glow_property );

	struct report_hit_t {
		PAD( 0x18 );
		vec3_t m_position;
	};
	static void* report_hit( report_hit_t* hit );
	static void setup_move(c_player_movement_services* mv_services, c_user_cmd* user_cmd, c_move_data* mv_data);
};