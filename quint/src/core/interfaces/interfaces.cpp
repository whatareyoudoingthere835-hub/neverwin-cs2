#include "interfaces.h"
#include <core/hooks/modules.h>

template<typename T>
inline T c_interfaces::find_interface( void* module_handle, const char* name ) {
	using fn = T( * )( const char*, int* );
	const auto func = reinterpret_cast<fn>( GetProcAddress( (HMODULE)module_handle, ( xx( "CreateInterface" ) ) ) );
	if ( !func )
		return nullptr;

	auto res = func( name, nullptr );

	LOG( std::format( "{}{} at {}", xx( "found interface: " ), name, static_cast<void*>( res ) ).c_str( ) );

	return res;
}

void c_interfaces::init( void ) {
	m_swap_chain = **g_modules->m_rendersystemdx11.find(xx("48 89 2D E4 21 46 00 66 0F 7F 05 E4 21 46 00 FF")).relative(3, 7).as< c_swap_chain_dx_11*** >();
	if (FAILED(m_swap_chain->m_dxgi_swap_chain->GetDevice(__uuidof(ID3D11Device), (void**)&m_device))) {
		LOG_ERROR("failed to get device");
	}
	else {
		m_device->GetImmediateContext(&m_device_context);
	}
	// Оффсеты из дампа build 14176 (20.08.2026) — вместо сигнатур.
	const auto client_base = reinterpret_cast<std::uintptr_t>(g_modules->m_client.get());
	this->m_csgo_input = *reinterpret_cast<c_csgo_input**>(client_base + 0x23BFB20);       // dwCSGOInput
	this->m_global_vars = *reinterpret_cast<c_global_variables**>(client_base + 0x2095D48); // dwGlobalVars
	this->m_entity_system = *reinterpret_cast<c_entity_system**>(client_base + 0x2555050);   // dwEntityList (CGameEntitySystem::m_list)
	//this->m_hitbox_system = *g_modules->m_client.find( xx( "48 8B 0D ? ? ? ? 48 8B D0 C7 44 24 ? ? ? ? ? 48 C7 44 24 ? ? ? ? ? E8 ? ? ? ? F3 0F 10 85" ) ).relative( 3, 7 ).as<c_hitbox_system**>( );
	this->m_phys2world = *g_modules->m_client.find(xx("4C 8B 25 ? ? ? ? 24")).relative(3, 7).as<c_vphys2world**>();
	this->m_world = *g_modules->m_client.find(xx("4C 8B 05 ? ? ? ? 4D 85 C0 74 ? 83 F9 ? 74 ? 8B C1 25 ? ? ? ? 8B D0 48 C1 E8 ? ? ? ? ? 4D 85 DB")).relative(3, 7).as<c_world**>();
	this->m_game_event_manager = *g_modules->m_client.find(xx("48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 50 ? 4C 8B 05")).relative(3, 7).as<c_game_event_manager2**>();
	this->m_pvs = g_modules->m_engine2.find(xx("48 8D 0D ? ? ? ? 33 D2 FF 50")).relative(3, 7).as<c_engine_pvs_manager*>();
	this->m_game_particle_manager = *g_modules->m_client.find(xx("48 8B 0D ? ? ? ? 41 B8 ? ? ? ? F3 0F 11 74 24")).relative(3, 7).as<c_game_particle_manager**>();

	this->m_schema_system = find_interface<c_schema_system*>(g_modules->m_schemasystem.get(), xx("SchemaSystem_001"));
	this->m_vphysics2 = find_interface<c_vhpysics*>(g_modules->m_vphysics.get(), xx("VPhysics2_Interface_001"));
	this->m_input_system = find_interface<c_input_system*>(g_modules->m_inputsystem.get(), xx("InputSystemVersion001"));

	this->m_localize = find_interface<c_localize*>(g_modules->m_localize.get(), xx("Localize_001"));
	this->m_engine_convar = find_interface<c_engine_cvar*>(g_modules->m_tier0.get(), xx("VEngineCvar007"));

	this->m_material_system = find_interface<c_material_system_2*>(g_modules->m_materialsystem2.get(), xx("VMaterialSystem2_001"));
	this->m_network_client_services = find_interface<c_network_game_client_services*>(g_modules->m_engine2.get(), xx("NetworkClientService_001"));
	this->m_engine = find_interface<c_engine_client*>(g_modules->m_engine2.get(), xx("Source2EngineToClient001"));
	this->m_prediction = find_interface<c_prediction*>(g_modules->m_client.get(), xx("Source2ClientPrediction001"));
	this->m_source2_client = find_interface<c_source2_client*>(g_modules->m_client.get(), xx("Source2Client002"));
	this->m_file_system = find_interface<c_file_system*>(g_modules->m_filesystem_stdio.get(), xx("VFileSystem017"));
	
	this->m_scene_system = find_interface<c_scene_system*>(g_modules->m_scenesystem.get(), xx("SceneSystem_002"));
	this->m_resource_system = find_interface<c_resource_system*>(g_modules->m_resourcesystem.get(), xx("ResourceSystem013"));
	this->m_particle_system_mgr = find_interface<c_particle_system_mgr*>(g_modules->m_particles.get(), xx("ParticleSystemMgr003"));
	this->m_panorama_ui_engine = find_interface<c_panorama_ui_engine*>(g_modules->m_panorama.get(), xx("PanoramaUIEngine001"));

	auto steamApi = GetModuleHandleA("steam_api64.dll");
	if (!steamApi)
		return;

	ISteamClient* m_steam_client = reinterpret_cast<fn_steam_client>(GetProcAddress((HMODULE)steamApi, ("SteamClient")))();

	std::uint64_t steam_user = reinterpret_cast<tt>(GetProcAddress((HMODULE)steamApi, ("SteamAPI_GetHSteamUser")))();
	std::uint64_t steam_pipe = reinterpret_cast<tt>(GetProcAddress((HMODULE)steamApi, ("SteamAPI_GetHSteamPipe")))();

	this->m_steam_friends = m_steam_client->GetISteamFriends((std::uint64_t)steam_user, (std::uint64_t)steam_pipe, "SteamFriends017");
	this->m_steam_utils = m_steam_client->GetISteamUtils((std::uint64_t)steam_pipe, "SteamUtils010");
}

void c_interfaces::create_render_target( ) {
	destroy_render_target();

	m_swap_chain = **g_modules->m_rendersystemdx11.find(xx("48 89 2D E4 21 46 00 66 0F 7F 05 E4 21 46 00 FF")).relative(3, 7).as< c_swap_chain_dx_11*** >();

	if ( !m_swap_chain || !m_swap_chain->m_dxgi_swap_chain ) {
		LOG_ERROR( xx( "invalid swap chain" ) );
		return;
	}

	if ( !m_device ) {
		if ( FAILED( m_swap_chain->m_dxgi_swap_chain->GetDevice( __uuidof( ID3D11Device ), (void**)&m_device ) ) ) {
			LOG_ERROR( xx( "failed to get device" ) );
			return;
		}
	}

	if ( !m_device ) {
		LOG_ERROR( xx( "device is null" ) );
		return;
	}

	if ( !m_device_context ) {
		m_device->GetImmediateContext( &m_device_context );
	}

	if ( !m_device_context ) {
		LOG_ERROR( xx( "failed to get device context" ) );
		return;
	}

	static const auto GetCorrectDXGIFormat = [ ]( DXGI_FORMAT eCurrentFormat ) {
		switch ( eCurrentFormat ) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		return eCurrentFormat;
		};

	DXGI_SWAP_CHAIN_DESC sd;
	if ( FAILED( m_swap_chain->m_dxgi_swap_chain->GetDesc( &sd ) ) ) {
		LOG_ERROR( xx( "failed to get swap chain desc" ) );
		return;
	}

	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = m_swap_chain->m_dxgi_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &pBackBuffer ) );
	if ( FAILED( hr ) ) {
		LOG_ERROR( xx( "failed to get back buffer" ) );
		return;
	}

	if ( pBackBuffer ) {
		D3D11_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format = static_cast<DXGI_FORMAT>( GetCorrectDXGIFormat( sd.BufferDesc.Format ) );
		desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		
		hr = m_device->CreateRenderTargetView( pBackBuffer, &desc, &m_render_target_view );
		if ( FAILED( hr ) ) {
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
			hr = m_device->CreateRenderTargetView( pBackBuffer, &desc, &m_render_target_view );
			if ( FAILED( hr ) ) {
				LOG_ERROR( xx( "failed to create render target view with D3D11_RTV_DIMENSION_TEXTURE2D" ) );
				LOG_ERROR( xx( "retrying.." ) );

				hr = m_device->CreateRenderTargetView( pBackBuffer, NULL, &m_render_target_view );
				if ( FAILED( hr ) ) {
					LOG_ERROR( xx( "failed to create render target view" ) );
				}
			}
		}
		
		pBackBuffer->Release( );
		pBackBuffer = nullptr;
	}
}

void c_interfaces::destroy_render_target( ) {
	if ( m_render_target_view ) {
		m_render_target_view->Release( );
		m_render_target_view = nullptr;
	}
}
