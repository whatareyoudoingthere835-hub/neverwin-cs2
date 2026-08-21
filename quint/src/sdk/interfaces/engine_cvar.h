#pragma once
#include <vector>
#include <utils/memory.h>
#include <sdk/datatypes/fnv1a.h>


enum cvar_flags {
	FCVAR_NONE = 0,
	FCVAR_UNREGISTERED = ( 1 << 0 ),
	FCVAR_DEVELOPMENTONLY = ( 1 << 1 ),
	FCVAR_GAMEDLL = ( 1 << 2 ),
	FCVAR_CLIENTDLL = ( 1 << 3 ),
	FCVAR_HIDDEN = ( 1 << 4 ),
	FCVAR_PROTECTED = ( 1 << 5 ),
	FCVAR_SPONLY = ( 1 << 6 ),
	FCVAR_ARCHIVE = ( 1 << 7 ),
	FCVAR_NOTIFY = ( 1 << 8 ),
	FCVAR_USERINFO = ( 1 << 9 ),
	FCVAR_CHEAT = ( 1 << 14 ),
	FCVAR_PRINTABLEONLY = ( 1 << 10 ),
	FCVAR_UNLOGGED = ( 1 << 11 ),
	FCVAR_NEVER_AS_STRING = ( 1 << 12 ),
	FCVAR_REPLICATED = ( 1 << 13 ),
	FCVAR_DEMO = ( 1 << 16 ),
	FCVAR_DONTRECORD = ( 1 << 17 ),
	FCVAR_RELOAD_MATERIALS = ( 1 << 20 ),
	FCVAR_RELOAD_TEXTURES = ( 1 << 21 ),
	FCVAR_NOT_CONNECTED = ( 1 << 22 ),
	FCVAR_MATERIAL_SYSTEM_THREAD = ( 1 << 23 ),
	FCVAR_ARCHIVE_XBOX = ( 1 << 24 ),
	FCVAR_ACCESSIBLE_FROM_THREADS = ( 1 << 25 ),
	FCVAR_SERVER_CAN_EXECUTE = ( 1 << 28 ),
	FCVAR_SERVER_CANNOT_QUERY = ( 1 << 29 ),
	FCVAR_CLIENTCMD_CAN_EXECUTE = ( 1 << 30 ),
	FCVAR_MATERIAL_THREAD_MASK = ( FCVAR_RELOAD_MATERIALS | FCVAR_RELOAD_TEXTURES | FCVAR_MATERIAL_SYSTEM_THREAD )
};

using cvar_iterator = unsigned long long;

union convar_value {
	bool i1;
	short i16;
	unsigned short u16;
	int i32;
	unsigned int u32;
	long long i64;
	unsigned long long u64;
	float fl;
	double db;
	const char* sz;
};

class c_convar {
public:
	const char* m_name;
	c_convar* m_next;
	char pad_0008[0x10];
	const char* m_description;
	unsigned int m_type;
	unsigned int m_registered;
	unsigned int m_flags;
	char padding_2[0x24];
	convar_value m_value;

	bool get_bool( ) const {
		return m_value.i1;
	}

	int get_int( ) const {
		return m_value.i32;
	}

	float get_float( ) const {
		return m_value.fl;
	}
};

namespace {
	constexpr auto hidden_cvar_flags = ( FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY );
}

struct convar_entry_t {
	c_convar* m_convar;
	uint16_t m_prev;
	uint16_t m_next;
};

class c_engine_cvar {
public:
	PAD(80);
	convar_entry_t* m_entries;
private:
	std::vector<c_convar*> hidden_cvars{};
public:
	c_convar* find_by_name( const char* name ) {
		return find_by_hash( fnv1a::hash_64( name ) );
	}

	c_convar* find_by_hash( uint64_t hash ) const {
		for ( uint16_t i = 0; i != static_cast< uint16_t >( -1 ); i = m_entries[ i ].m_next ) {
			c_convar* convar = m_entries[ i ].m_convar;
			if ( !convar )
				continue;

			if ( fnv1a::hash_64( convar->m_name ) == hash )
				return convar;
		}
		return nullptr;
	}

	void lock_hidden_vars( ) {
		for ( auto i : hidden_cvars )
			i->m_flags |= hidden_cvar_flags;
	}

	void unlock_hidden_vars( ) const {
		for ( uint16_t i = 0; i != static_cast< uint16_t >( -1 ); i = m_entries[ i ].m_next ) {
			c_convar* convar = m_entries[ i ].m_convar;
			if ( !convar )
				continue;

			if ( convar->m_flags & FCVAR_HIDDEN )
				convar->m_flags &= ~FCVAR_HIDDEN;

			if ( convar->m_flags & FCVAR_DEVELOPMENTONLY )
				convar->m_flags &= ~FCVAR_DEVELOPMENTONLY;
		}
	}
};
