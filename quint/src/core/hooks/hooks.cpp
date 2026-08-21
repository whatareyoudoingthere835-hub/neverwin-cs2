#include "hooks.h"

#include "hooks_include.h"
#include "modules.h"
#include <utils/memory.h>

#include <sdk/interfaces/scene_system.h>

std::string get_install_path( ) {
	char path[ MAX_PATH ] = { 0 };
	GetModuleFileNameA( NULL, path, MAX_PATH );
	return std::string( path );
}

void c_hooks::init( void ) {
	MH_Initialize( );



	g_ctx->m_running_path = get_install_path( );

	{ // rendersystemdx11.dll
		this->m_render_system.m_present.hook( (void*)&c_render_system_hooks::present, g_memory->get_method<void*>( g_interfaces->m_swap_chain->m_dxgi_swap_chain, 8 ) );
		this->m_render_system.m_resize_buffers.hook( (void*)&c_render_system_hooks::resize_buffers, g_memory->get_method<void*>( g_interfaces->m_swap_chain->m_dxgi_swap_chain, 13 ) );

		IDXGIDevice* dxgi_device = NULL;
		g_interfaces->m_device->QueryInterface( IID_PPV_ARGS( &dxgi_device ) );

		IDXGIAdapter* dxgi_adapter = NULL;
		dxgi_device->GetAdapter( &dxgi_adapter );

		IDXGIFactory* dxgi_factory = NULL;
		dxgi_adapter->GetParent( IID_PPV_ARGS( &dxgi_factory ) );

		this->m_render_system.m_create_swap_chain.hook( (void*)&c_render_system_hooks::create_swap_chain, g_memory->get_method<void*>( dxgi_factory, 10 ) );

		dxgi_device->Release( );
		dxgi_device = nullptr;
		dxgi_adapter->Release( );
		dxgi_adapter = nullptr;
		dxgi_factory->Release( );
		dxgi_factory = nullptr;
	}

	{ // client.dll

		this->m_client.m_mouse_input_enabled.hook( (void*)&c_client_hooks::mouse_input_enabled, g_memory->get_method<void*>( g_interfaces->m_csgo_input, 23 ) );
		this->m_client.m_is_relative_mouse_mode.hook( (void*)&c_client_hooks::is_relative_mouse_mode, g_memory->get_method<void*>( g_interfaces->m_input_system, 76 ) );
		this->m_client.m_anti_tamper.hook( (void*)&c_client_hooks::anti_tamper, g_modules->m_client.find(xx("40 53 41 57 48 83 EC ? 48 89 74 24 ? 48 8B F1")).as<void*>());
		this->m_client.m_create_move.hook( (void*)&c_client_hooks::create_move, g_memory->get_method<void*>( g_interfaces->m_csgo_input, 5 ) );
		this->m_client.m_handle_camera_angles.hook( (void*)&c_client_hooks::handle_camera_angles, g_memory->get_method<void*>( g_interfaces->m_csgo_input, 8 ) );
		this->m_client.m_get_world_fov.hook( (void*)&c_client_hooks::get_world_fov, g_modules->m_client.find(xx("E8 ? ? ? ? ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 83 C4 ? 41 5E 5F 5E C3 ? ? ? ? ? ? ? ? ? ? ? ? 48 8B C4")).relative(1, 5).as<void*>());
		this->m_client.m_level_init.hook( (void*)&c_client_hooks::level_init, g_modules->m_client.find(xx("48 89 74 24 ? 57 48 83 EC 30 48 8B 0D ? ? ? ? 48 8B FA")).as<void*>());
		this->m_client.m_level_shutdown.hook( (void*)&c_client_hooks::level_shutdown, g_modules->m_client.find(xx("48 83 EC ? 48 8B 0D ? ? ? ? 48 8D 15 ? ? ? ? 45 33 C9 45 33 C0 48 8B 01 FF 50 ? 48 85 C0 74 ? 48 8B 0D ? ? ? ? 48 8B D0 4C 8B 01 41 FF 50 ? 48 83 C4")).as<void*>());
		this->m_client.m_add_entity.hook( (void*)&c_client_hooks::add_entity, g_modules->m_client.find(xx("48 89 74 24 ? 57 48 83 EC ? 41 B9 ? ? ? ? 41 8B C0 41 23 C1 48 8B F2 41 83 F8 ? 48 8B F9 44 0F 45 C8 41 81 F9 ? ? ? ? 73 ? FF 81")).as<void*>());
		this->m_client.m_remove_entity.hook( (void*)&c_client_hooks::remove_entity, g_modules->m_client.find(xx("48 89 74 24 ? 57 48 83 EC ? 41 B9 ? ? ? ? 41 8B C0 41 23 C1 48 8B F2 41 83 F8 ? 48 8B F9 44 0F 45 C8 41 81 F9 ? ? ? ? 73 ? FF 89")).as<void*>());
			
		this->m_client.m_matricies_for_view.hook( (void*)&c_client_hooks::get_matrix_for_view, g_modules->m_client.find(xx("48 8B C4 48 89 68 ? 48 89 70 ? 57 48 81 EC ? ? ? ? 0F 29 70 ? 49 8B F1")).as<void*>());
		this->m_client.m_calc_viewmodel.hook( (void*)&c_client_hooks::calc_viewmodel, g_modules->m_client.find(xx("40 55 53 56 41 56 41 57 48 8B EC")).as<void*>());
		this->m_client.m_setup_map_info.hook( (void*)&c_client_hooks::setup_map_info, g_modules->m_client.find(xx("48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 48 81 EC ? ? ? ? 0F 29 70 E8 48 8B EA")).as<void*>());

		this->m_client.m_override_view.hook( (void*)&c_client_hooks::override_view, g_modules->m_client.find(xx("40 57 48 83 EC ? 48 8B FA E8 ? ? ? ? BA")).as<void*>());
		this->m_client.m_frame_stage_notify.hook( (void*)&c_client_hooks::frame_stage_notify, g_memory->get_method<void*>(g_interfaces->m_source2_client, 36));
	
		
		this->m_client.m_draw_scope.hook( (void*)&c_client_hooks::draw_scope, g_modules->m_client.find( xx( "48 8B C4 53 57 48 83 EC ? 48 8B FA" ) ).as<void*>( ) );
		this->m_client.m_draw_overhead.hook( (void*)&c_client_hooks::draw_overhead, g_modules->m_client.find( xx( "40 53 48 83 EC ? 48 8B D9 83 FA ? 75 ? 48 8B 0D ? ? ? ? 48 8D 54 24 ? 48 8B 01 FF 90 ? ? ? ? 8B 10" ) ).as<void*>( ) );
		this->m_client.m_smoke_volume_drawarray.hook( (void*)&c_client_hooks::smoke_volume_scene_object_drawarray, g_modules->m_client.find( xx( "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 48 8B 9C 24 ? ? ? ? 4D 8B F8" ) ).as<void*>( ) );
		this->m_client.m_draw_flash_overlay.hook( (void*)&c_client_hooks::draw_flash_overlay, g_modules->m_client.find( xx( "85 D2 0F 88 ? ? ? ? 48 89 4C 24" ) ).as<void*>( ) );
		this->m_client.m_first_person_legs.hook( (void*)&c_client_hooks::first_person_legs, g_modules->m_client.find( xx( "40 55 53 56 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? F2 0F 10 42" ) ).as<void*>( ) );
		this->m_client.m_handle_glow.hook( (void*)&c_client_hooks::handle_glow, g_modules->m_client.find( xx( "E8 ? ? ? ? F3 0F 10 BE ? ? ? ? 48 8B CF" ) ).relative(1, 5).as<void*>( ) );
		this->m_client.m_is_glow.hook( (void*)&c_client_hooks::m_is_glow, g_modules->m_client.find( xx( "40 53 48 83 EC 20 48 8B 54" ) ).as<void*>( ) );
		this->m_client.m_report_hit.hook( (void*)&c_client_hooks::report_hit, g_modules->m_client.find( xx( "40 53 48 83 EC ? 48 8D 05 ? ? ? ? 48 8D 59 ? 48 89 01 F6 03 ? 74 ? 48 8B CB E8 ? ? ? ? 48 8B 0B 0F B6 C1 D0 E8 A8 ? 74 ? 48 8D 59 ? 48 85 DB 74 ? 48 8B CB E8 ? ? ? ? BA ? ? ? ? 48 8B CB 48 83 C4 ? 5B E9 ? ? ? ? 48 83 C4 ? 5B C3 CC CC CC CC CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 F6 41 ? ? 74 ? 48 83 C1 ? E8 ? ? ? ? EB ? 48 8B 41 ? 48 83 E0 ? 48 85 C0 75 ? 48 8D 4F ? E8 ? ? ? ? 48 8D 4F ? E8 ? ? ? ? 48 8B 5F ? 0F B6 C3 D0 E8 A8 ? 74 ? 48 83 C3 ? 74 ? 48 8B CB E8 ? ? ? ? BA ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B 5C 24 ? 48 83 C4 ? 5F C3 CC CC CC CC CC CC 48 89 5C 24 ? 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B D9" ) ).as<void*>( ) );
		//this->m_client.m_update_global_vars.hook( (void*)&c_client_hooks::update_global_variables, g_modules->m_client.find(xx("48 8B 0D ? ? ? ? 4C 8D 05 ? ? ? ? 48 85 D2")).as<void*>());
	}

	{ // scenesystem.dll
		this->m_scene_system.m_generate_primitives.hook( (void*)&c_scene_system_hooks::generate_primitives, g_memory->get_method<void*>( g_interfaces->m_scene_system->get_scene_object_desc( xx( "AnimatableSceneObjectDesc" ) ), 4 ) );
		this->m_scene_system.m_draw_aggregate_sceneobject_array.hook( (void*)&c_scene_system_hooks::draw_aggregate_sceneobject_array, g_modules->m_scenesystem.find("48 8B C4 48 89 50 ? 48 89 48 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70").as<uintptr_t*>());
		this->m_scene_system.m_light_scene_object.hook( (void*)&c_scene_system_hooks::light_scene_object, g_modules->m_scenesystem.find( xx( "48 89 54 24 ? 55 57 41 56 48 83 EC" ) ).as<void*>( ) );
		this->m_scene_system.m_draw_skybox_array.hook( (void*)&c_scene_system_hooks::draw_skybox_array, g_modules->m_scenesystem.find( xx( "45 85 C9 0F 8E ? ? ? ? 4C 8B DC 55" ) ).as<void*>( ) );
		this->m_scene_system.m_tonemap_debug.hook( (void*)&c_scene_system_hooks::tonemap_render_debug, g_modules->m_scenesystem.find( xx( "40 53 48 81 EC ? ? ? ? F2 0F 10 51" ) ).as<void*>( ) );
	//	this->m_scene_system.m_base_draw_array.hook( (void*)&c_scene_system_hooks::base_draw_array, g_memory->get_method<void*>( g_interfaces->m_scene_system->get_scene_object_desc( xx( "BaseSceneObjectDesc" ) ), 1 ) );
	}



	{ // engine hooks
		this->m_engine.m_setup_blur.hook( (void*)&c_engine_hooks::setup_blur, g_modules->m_engine2.find(xx("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 66 0F 6E CA 49 8B F8 66 0F 70 C9 ? 8B F2 48 8B D9 45 33 C9 48 8B C1 66 66 0F 1F 84 00 ? ? ? ? 66 0F 6F C1 4C 8D 15 ? ? ? ? 66 0F 76 00 0F 50 C8 85 C9 75 ? 41 FF C1 48 83 C0 ? 41 83 F9 ? 72 ? 48 8B 83")).as<void*>());
	}
}
