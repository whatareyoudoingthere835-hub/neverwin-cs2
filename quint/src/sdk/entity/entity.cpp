#include "entity.h"
#include "pawn.h"

#include <cstring>

#include <sdk/interfaces/schema_system.h>

matrix3x4_t bone_data_t::to_matrix( ) const {
	matrix3x4_t matrix;

	const vec4_t& q = m_rot;
	const vec3_t& pos = m_pos;

	const float xx = q.x * q.x;
	const float yy = q.y * q.y;
	const float zz = q.z * q.z;
	const float xy = q.x * q.y;
	const float xz = q.x * q.z;
	const float yz = q.y * q.z;
	const float wx = q.w * q.x;
	const float wy = q.w * q.y;
	const float wz = q.w * q.z;

	matrix[ 0 ][ 0 ] = 1.0f - 2.0f * ( yy + zz );
	matrix[ 0 ][ 1 ] = 2.0f * ( xy - wz );
	matrix[ 0 ][ 2 ] = 2.0f * ( xz + wy );
	matrix[ 0 ][ 3 ] = pos.x;

	matrix[ 1 ][ 0 ] = 2.0f * ( xy + wz );
	matrix[ 1 ][ 1 ] = 1.0f - 2.0f * ( xx + zz );
	matrix[ 1 ][ 2 ] = 2.0f * ( yz - wx );
	matrix[ 1 ][ 3 ] = pos.y;

	matrix[ 2 ][ 0 ] = 2.0f * ( xz - wy );
	matrix[ 2 ][ 1 ] = 2.0f * ( yz + wx );
	matrix[ 2 ][ 2 ] = 1.0f - 2.0f * ( xx + yy );
	matrix[ 2 ][ 3 ] = pos.z;

	return matrix;
}

c_schema_class_binding_base* c_entity_instance::get_class_binding_base( )
{
	c_schema_class_binding_base* class_binding_base = nullptr;
	g_memory->call_virtual<c_schema_class_binding_base*>( this, 46, &class_binding_base );
	return class_binding_base;
}

const char* c_entity_instance::get_class_name( )
{
	const auto class_binding_base = get_class_binding_base( );
	return class_binding_base->get_name( );
}

bool c_entity_instance::is_player_pawn( )
{
	const char* name = get_class_name( );
	return name && std::strstr( name, "C_CSPlayerPawn" ) != nullptr;
}

bool c_entity_instance::is_player_controller( )
{
	const char* name = get_class_name( );
	return name && std::strstr( name, "CCSPlayerController" ) != nullptr;
}

bool c_entity_instance::is_player( )
{
	return is_player_pawn( ) || is_player_controller( );
}

c_base_handle c_entity_instance::get_handle( )
{
	auto identity = m_pEntity( );
	if ( identity )
		return c_base_handle( identity->index( ), identity->get_serial_number( ) - ( identity->flags( ) & 1 ) );

	return {};
}

bool c_entity_identity::is_valid( )
{
	return index( ) != INVALID_EHANDLE_INDEX;
}

int c_entity_identity::get_serial_number( )
{
	return index( ) >> NUM_SERIAL_NUM_SHIFT_BITS;
}

int c_entity_identity::get_entry_index( )
{
	if ( is_valid( ) )
		return index( ) & ENT_ENTRY_MASK;

	return ENT_ENTRY_MASK;
}


c_skeleton_instance* c_game_scene_node::get_skeleton_instace( )
{
	return g_memory->call_virtual<c_skeleton_instance*>( this, 13 );
}


void c_game_scene_node::set_mesh_group_mask( uint64_t mask )
{
	using fn_set_mesh_group_mask = void( __fastcall* )( void*, uint64_t );
	static auto fn = g_modules->m_client.find( xx( "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8D 99 ? ? ? ? 48 8B 71" ) ).as<fn_set_mesh_group_mask>( );
	fn( this, mask );
}

uint64_t c_collision::get_or_create_traced_collision_mask( )
{
	using fn_get_or_create_traced_collision_mask = uint64_t( __fastcall* )( void* );
	static auto fn = g_modules->m_client.find( xx( "40 53 48 83 EC ? 48 8B 51 ? 48 8B D9 8B 8A" ) ).as<fn_get_or_create_traced_collision_mask>( );
	return fn( this );
}

bool c_base_entity::is_weapon2()
{
	return g_memory->call_virtual<bool>(this, 162);
}
bool c_base_entity::is_weapon( )
{
	return g_memory->call_virtual<bool>( this, 152);
}

void c_base_entity::set_model( const char* name )
{
	using fn_set_model = void( __fastcall* )( void*, const char* );
	static auto fn = g_modules->m_client.find( xx( "40 53 48 83 EC 20 48 8B D9 4C 8B C2 48 8B 0D ? ? ? ? 48 8D 54 24" ) ).as<fn_set_model>( );
	fn( this, name );
}

c_hitbox* c_model::get_hitbox( const std::int32_t index ) {
	if ( m_rendermesh_count <= 0 || !m_render_meshes )
		return nullptr;

	c_render_mesh* meshes = m_render_meshes->m_meshes;
	if ( !meshes )
		return nullptr;

	c_render_mesh mesh = meshes[ 0 ];
	c_hitbox_sets* hitbox_sets = mesh.m_hitbox_sets;
	if ( !hitbox_sets )
		return nullptr;

	if ( hitbox_sets[ 0 ].m_hitbox_count <= 0 || index > hitbox_sets[ 0 ].m_hitbox_count )
		return nullptr;

	c_hitbox* hitbox = hitbox_sets[ 0 ].m_hitbox;
	if ( !hitbox )
		return nullptr;

	return &hitbox[ index ];
}

int c_model::get_hitbox_count( ) {
	if ( m_rendermesh_count <= 0 || !m_render_meshes )
		return -1;

	c_render_mesh* meshes = m_render_meshes->m_meshes;
	if ( !meshes )
		return -1;

	c_render_mesh mesh = meshes[ 0 ];
	c_hitbox_sets* hitbox_sets = mesh.m_hitbox_sets;
	if ( !hitbox_sets )
		return -1;

	return hitbox_sets[ 0 ].m_hitbox_count;
}

void* c_skeleton_instance::get_physics_prop_multiplayer( ) {
	static void* random_num_ptr = g_modules->m_client.find(xx("48 63 0D ? ? ? ? 48 8B 40 ? 0F B7 14 48 B8 ? ? ? ? 66 3B D0 74 ? 48 8B 3C 3A")).relative(3, 7).as<void*>();
	int dword_1817A5050 = *( int* )( random_num_ptr );

	_QWORD* m_entity_indentity = ( _QWORD* )this->m_pawn( )->m_pEntity( );

	if ( !*m_entity_indentity )
		return nullptr;

	auto v9 = *( unsigned __int16* )( *( _QWORD* )( m_entity_indentity[ 1 ] + 88LL ) + 2LL * dword_1817A5050 );

	return (void*)(( _WORD )v9 == 0xFFFF ? 0LL : *( _QWORD* )( v9 + *m_entity_indentity ));
}