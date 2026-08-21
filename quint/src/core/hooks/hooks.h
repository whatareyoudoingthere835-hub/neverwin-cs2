#pragma once

#include "quint_hook.h"

class c_hooks {
public:
	struct {
		quint_hook_t m_mouse_input_enabled{ xx( "mouse_input_enabled" ) };
		quint_hook_t m_anti_tamper{ xx("anti_tamper") };
		quint_hook_t m_is_relative_mouse_mode{ xx( "is_relative_mouse_mode" ) };
		quint_hook_t m_create_move{ xx( "create_move" ) };
		quint_hook_t m_get_world_fov{ xx( "get_world_fov" ) };
		quint_hook_t m_calc_viewmodel{ xx("calc_viewmodel") };
		quint_hook_t m_level_init{ xx( "level_init" ) };
		quint_hook_t m_level_shutdown{ xx( "level_shutdown" ) };
		quint_hook_t m_add_entity{ xx( "add_entity" ) };
		quint_hook_t m_remove_entity{ xx( "remove_entity" ) };
		quint_hook_t m_matricies_for_view{ xx( "matricies_for_view" ) };
		quint_hook_t m_override_view{ xx( "override_view" ) };
		quint_hook_t m_frame_stage_notify{ xx( "frame_stage_notify" ) };
		quint_hook_t m_handle_camera_angles{ xx( "handle_camera_angles" ) };
		
		quint_hook_t m_setup_map_info{ xx("setup_map_info") };
		quint_hook_t m_draw_scope{ xx( "draw_scope" ) };
		quint_hook_t m_draw_overhead{ xx( "draw_overhead" ) };
		quint_hook_t m_smoke_volume_drawarray{ xx( "smoke_volume_drawarray" ) };
		quint_hook_t m_draw_flash_overlay{ xx( "draw_flash_overlay" ) };
		quint_hook_t m_first_person_legs{ xx( "first_person_legs" ) };
		quint_hook_t m_handle_glow{ xx("handle_glow") };
		quint_hook_t m_is_glow{ xx("m_is_glow") };
		quint_hook_t m_report_hit { xx( "report_hit" ) };
		quint_hook_t m_setup_move{ xx("setup_move") };
		//quint_hook_t m_update_global_vars{ xx("update_global_vars") };
	} m_client;

	struct {
		quint_hook_t m_present{ xx( "present" ) };
		quint_hook_t m_resize_buffers{ xx( "resize_buffers" ) };
		quint_hook_t m_create_swap_chain{ xx( "create_swap_chain" ) };
	} m_render_system;

	struct {
		quint_hook_t m_setup_blur{ xx("setup_blur") };
	} m_engine;

	struct {
		quint_hook_t m_generate_primitives{ xx( "generate_primitives" ) };
		quint_hook_t m_draw_aggregate_sceneobject_array{ xx( "draw_aggregate_sceneobject_array" ) };
		quint_hook_t m_light_scene_object{ xx( "light_scene_object" ) };
		quint_hook_t m_draw_skybox_array{ xx( "draw_skybox_array" ) };
		quint_hook_t m_tonemap_debug{ xx( "tonemap_debug" ) };
		quint_hook_t m_draw_aggregate_sceneobject{ xx( "draw_aggregate_sceneobject" ) };
		quint_hook_t m_base_draw_array{ xx( "base_draw_array" ) };
	} m_scene_system;



	struct {
		quint_hook_t m_test_hook_1{ xx( "test_hook_1" ) };
	} m_test;
public:
	void init( void );
};
inline auto g_hooks = std::make_unique<c_hooks>( );
