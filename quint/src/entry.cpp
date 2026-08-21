#include "entry.h"

#include <core/hooks/modules.h>
#include <core/hooks/hooks.h>
#include <core/interfaces/interfaces.h>
#include <sdk/schema/schema.h>
#include <cheat/input.h>
#include <cheat/config/config_system.h>
#include <cheat/features/visuals/chams.h>
#include <cheat/features/visuals/visuals.h>
#include <cheat/features/skins/skins.h>
#include <cheat/features/visuals/grenade.h>
#include <cheat/features/visuals/overlay_features.h>


static HMODULE s_quint_module = nullptr;

static void qlog( const char* msg )
{
	// Диагностика для ручного маппинга: пишем в quint.log рядом с DLL и в
	// %TEMP%\quint.log через чистое kernel32 API (CreateFileW + WriteFile).
	const auto write_to = [&]( const wchar_t* path ) {
		const HANDLE f = CreateFileW( path, FILE_APPEND_DATA, FILE_SHARE_READ,
		                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( f == INVALID_HANDLE_VALUE )
			return;
		char line[ 512 ]{};
		const char* p = "[quint] ";
		std::size_t n = 0;
		for ( ; *p && n + 1 < sizeof( line ); ++n )
			line[ n ] = *p++;
		for ( ; *msg && n + 2 < sizeof( line ); ++n )
			line[ n ] = *msg++;
		line[ n++ ] = '\r';
		line[ n++ ] = '\n';
		DWORD written = 0;
		WriteFile( f, line, static_cast<DWORD>( n ), &written, nullptr );
		CloseHandle( f );
	};

	wchar_t path[ MAX_PATH ]{};
	GetModuleFileNameW( s_quint_module, path, MAX_PATH );
	if ( path[ 0 ] ) {
		wchar_t* slash = wcsrchr( path, L'\\' );
		if ( slash ) {
			wcscpy( slash + 1, L"quint.log" );
			write_to( path );
		}
	}

	path[ 0 ] = 0;
	if ( GetTempPathW( MAX_PATH, path ) ) {
		wcscat( path, L"quint.log" );
		write_to( path );
	}
}

void c_entry::entry( void* moduleptr ) {
	s_quint_module = static_cast<HMODULE>( moduleptr );
	qlog( "entry thread started" );
#ifdef _DEBUG
	g_logging->init( xx( "quint-console" ) );
#endif
	LOG( xx( "quint developer console" ) );

	qlog( "before: modules init" );
	g_modules->init( );
	qlog( "after: modules init" );
	LOG( xx( "modules init" ) );

	qlog( "before: input init" );
	g_input->init( );
	qlog( "after: input init" );
	LOG( xx( "input init" ) )

	qlog( "before: interfaces init" );
	g_interfaces->init( );
	qlog( "after: interfaces init" );
	LOG( xx( "interfaces init" ) );

	qlog( "before: hooks init" );
	g_hooks->init( );
	qlog( "after: hooks init" );
	LOG( xx( "hooks init" ) );

	qlog( "before: config system init" );
	g_config_system->init( );
	qlog( "after: config system init" );
	LOG( xx( "config system init" ) );

	qlog( "before: chams init" );
	g_chams->init( );
	qlog( "after: chams init" );
	LOG( xx( "chams init" ) );

	g_event_listener->setup( {
		xx( "round_start" ), xx( "add_bullet_hit_marker" ), xx( "bullet_impact" ),
		xx( "player_hurt" ), xx( "player_death" ), xx( "weapon_fire" ), xx( "vote_cast" ),
		xx( "vote_started" ), xx( "item_purchase" ), xx( "bomb_defused" ),
		xx( "bomb_begindefuse" ), xx( "bomb_planted" ), xx( "bomb_beginplant" )
	} );
	LOG( xx( "events init" ) );

	qlog( "before: skins dumped" );
	g_skins->dump_items( );
	qlog( "after: skins dumped" );
	LOG( xx( "skins dumped" ) );
}

void c_entry::cleanup( void ) {
	//if (g_interfaces) {
	//	g_interfaces->destroy_render_target();
	//}
	//
	//for (auto& [pawn, model] : s_backtrack_models) {
	//	model.remove(REMOVE_ALL);
	//}
	//s_backtrack_models.clear();
	//
	//for (auto& [pawn, models] : s_onshot_models) {
	//	for (auto& model : models) {
	//		model.remove(REMOVE_ALL);
	//	}
	//}
	//s_onshot_models.clear();

	//s_onshot_skeletons.clear();
	//s_backtrack_skeletons.clear();
	//
	//for (auto& [hash, icon] : m_icons) {
	//	if (icon.texture_view) {
	//		icon.texture_view->Release();
	//	}
	//}
	//m_icons.clear();
	//
	//s_vecPredictedGrenades.clear();
	//
	//if (g_particle_mgr) {
	//	g_particle_mgr->clear_all_particles();
	//}
	//
	//if (g_overlay) {
	//	g_overlay->clear();
	//	g_overlay->clear_hitmarks();
	//}
}
