#pragma once
#include <math/math types/vector.h>
#include <context.h>

static vec2_t get_spread( int seed, float inaccuracy, float spread ) {

	vec2_t spread_vec = { 0.0f, 0.0f };

	using func_t = void( __fastcall* )(int16_t, int, int, std::uint32_t, float, float, float, float*, float*);
	static auto calc_spread_fn = g_modules->m_client.find( xx( "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 63 EA" ) ).as<func_t>( );

	calc_spread_fn(
		g_ctx->m_weapon_def_index,
		g_ctx->m_active_weapon_data->m_nNumBullets( ),
		0,
		seed,
		inaccuracy,
		spread,
		g_ctx->m_active_weapon->m_flRecoilIndex( ),
		&spread_vec.x,
		&spread_vec.y
	);

	return spread_vec;
}
