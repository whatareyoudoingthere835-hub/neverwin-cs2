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


void c_entry::entry( void* moduleptr ) {
#ifdef _DEBUG
	g_logging->init( xx( "quint-console" ) );
#endif
	LOG( xx( "quint developer console" ) );

	g_modules->init( );
	LOG( xx( "modules init" ) );

	g_input->init( );
	LOG( xx( "input init" ) )

	g_interfaces->init( );
	LOG( xx( "interfaces init" ) );

	g_hooks->init( );
	LOG( xx( "hooks init" ) );

	g_config_system->init( );
	LOG( xx( "config system init" ) );

	g_chams->init( );
	LOG( xx( "chams init" ) );

	g_event_listener->setup( {
		xx( "round_start" ), xx( "add_bullet_hit_marker" ), xx( "bullet_impact" ),
		xx( "player_hurt" ), xx( "player_death" ), xx( "weapon_fire" ), xx( "vote_cast" ),
		xx( "vote_started" ), xx( "item_purchase" ), xx( "bomb_defused" ),
		xx( "bomb_begindefuse" ), xx( "bomb_planted" ), xx( "bomb_beginplant" )
	} );
	LOG( xx( "events init" ) );

	g_skins->dump_items( );
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
