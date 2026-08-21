#include "scene_system_hooks.h"
#include <core/hooks/hooks.h>
#include <cheat/features/visuals/chams.h>
#include <sdk/interfaces/scene_system.h>
#include <core/interfaces/interfaces.h>
#include <cheat/features/visuals/skybox_changer.h>

void* c_scene_system_hooks::generate_primitives( c_animatable_scene_object_desc* desc, c_scene_animatable_object* object, void* a3, c_mesh_primitive_output_buffer* render_buf ) {
    static const auto original = g_hooks->m_scene_system.m_generate_primitives.get<decltype( &generate_primitives )>( );

    if ( g_ctx->m_local_controller )
        if ( g_chams->on_generate_primitives( desc, object, a3, render_buf ) )
            return nullptr;

    return original( desc, object, a3, render_buf );
}


int64_t c_scene_system_hooks::draw_aggregate_sceneobject_array(void* a1, void* a2, c_aggregate_object_arr* data)
{
	static const auto original = g_hooks->m_scene_system.m_draw_aggregate_sceneobject_array.get<decltype(&draw_aggregate_sceneobject_array)>();
	int64_t result = original(a1, a2, data);
	if (!data->data ||
		data->data->nIndex == -1 ||
		data->data->nCount == 0)
		return result;

	if (!g_interfaces->m_scene_system->data ||
		!g_interfaces->m_scene_system->data->lightData)
		return result;

	c_material_color_no_alpha color = c_material_color_no_alpha(GET_VAR(hellcolor, VISUALS_PATH(m_world_color)));

	if (GET_VAR(bool, VISUALS_PATH(m_enable_world_modulation))) {
		for (int i = 0; i < data->data->nCount; i++)
		{
			int index = data->data->nIndex + i;
			index = index << 5;

			*(c_material_color_no_alpha*)((uint64_t)g_interfaces->m_scene_system->data->lightData + index) = color;
		}
	}

	return result;
}

void* c_scene_system_hooks::light_scene_object( void* ptr, c_light_scene* object, void* unk )
{
	static const auto original = g_hooks->m_scene_system.m_light_scene_object.get<decltype( &light_scene_object )>( );

	if ( GET_VAR( bool, VISUALS_PATH( m_enable_lightnings_modulation ) ) )
		object->m_color = GET_VAR( hellcolor, VISUALS_PATH( m_lightings_color ) ) * 3;

	return original( ptr, object, unk );
}

void __fastcall c_scene_system_hooks::base_draw_array(scene_object_desc* __this, render_context* pCtx, c_mesh_primitive* pRenderList, int nNumRenderablesToDraw, c_scene_view const* pView, c_scene_layer * pLayer, void* a6)
{
	static const auto original = g_hooks->m_scene_system.m_base_draw_array.get<decltype(&base_draw_array)>();

	if (g_interfaces->m_engine->in_game() && g_interfaces->m_engine->is_connected())
	{
		for (auto i : std::ranges::iota_view{ 0, nNumRenderablesToDraw })
		{
			const auto material_name = std::string_view{ pRenderList[i].m_material->get_name() };

			if (material_name.find("sprays") != std::string::npos ||
				material_name.find("stickers") != std::string::npos ||
				material_name.find("patches") != std::string::npos ||
				material_name.find("glass") != std::string::npos ||
				material_name.find("blood") != std::string::npos ||
				material_name.find("materials/models/weapons") != std::string::npos ||
				material_name.find("weapons/models") != std::string::npos ||
				material_name.find("characters/models") != std::string::npos ||
				material_name.find("bombsite_") != std::string::npos ||
				material_name.find("compmat_") != std::string::npos ||
				material_name.find("_ignorez") != std::string::npos)
				continue;

			if (GET_VAR(bool, VISUALS_PATH(m_enable_sky_modulation)) && material_name.find("sun") != std::string::npos)
				pRenderList[i].m_color = GET_VAR(hellcolor, VISUALS_PATH(m_sun_color));
			else if (GET_VAR(bool, VISUALS_PATH(m_enable_sky_modulation)) && material_name.find("cloud") != std::string::npos)
				pRenderList[i].m_color = GET_VAR(hellcolor, VISUALS_PATH(m_clouds_color));
			else
			{
				if (GET_VAR(bool, VISUALS_PATH(m_enable_world_modulation)))
					pRenderList[i].m_color = GET_VAR(hellcolor, VISUALS_PATH(m_world_color));
			}
		}
	}

	original(__this, pCtx, pRenderList, nNumRenderablesToDraw, pView, pLayer, a6);
}

void c_scene_system_hooks::draw_skybox_array( void* a1, void* a2, c_draw_primitive_sky* draw_primitive, int count, void* a5, void* a6, void* a7 )
{
	static const auto original = g_hooks->m_scene_system.m_draw_skybox_array.get<decltype( &draw_skybox_array )>( );

	if (auto object = *reinterpret_cast<c_draw_primitive_sky**>((char*)draw_primitive + 0x18))
	{
		if (GET_VAR(bool, VISUALS_PATH(m_enable_sky_modulation))) {
			auto sky_clr = GET_VAR(hellcolor, VISUALS_PATH(m_sky_color));
			object->m_sky_color = { sky_clr.Value.x, sky_clr.Value.y, sky_clr.Value.z };
		}

	}
	original( a1, a2, draw_primitive, count, a5, a6, a7 );
}

void c_scene_system_hooks::tonemap_render_debug( void* a1, void* a2, void* a3 ) {
	static const auto original = g_hooks->m_scene_system.m_tonemap_debug.get<decltype(&tonemap_render_debug)>( );
	
	int value = GET_VAR( int, VISUALS_PATH( m_world_exposure ) );
	float brightness = GET_VAR( int, VISUALS_PATH( m_world_exposure ) ) == 0 ? 1.f : ( float )value / 100.f;
	g_memory->call_virtual<void>( a1, 5, brightness );

	return original( a1, a2, a3 );
}