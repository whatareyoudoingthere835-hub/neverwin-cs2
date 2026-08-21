#pragma once

#include <utils/memory.h>

#include "econ_item_system.h"

class c_debug_overlay_game_system {
public:
    void render_without_dots( bool should ) {
      //  return g_memory->call_virtual<void>( this, 10, should );
    }

    void add_line( const vec3_t& start, const vec3_t& end, const hellcolor& col, bool no_deth_test, float duration ) {
        return g_memory->call_virtual<void>( this, 12, start, end, col, no_deth_test, static_cast<double>( duration ) );
    }

    void add_box( const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs, const vec3_t& rot, const hellcolor& col, float duration ) {
        return g_memory->call_virtual<void>( this, 48 + 4U, origin, mins, maxs, rot, (uint8_t)(col.Value.x * 255 ), (uint8_t)( col.Value.y * 255 ), (uint8_t)( col.Value.z * 255 ), (uint8_t)( col.Value.w * 255 ), 0, static_cast<double>(duration), 0x3F800000);
    }

    void add_text( const vec3_t& origin, float duration, int line_offset, const char* text, ... ) {
        return g_memory->call_virtual<void>( this, 59 + 4U, origin, line_offset, static_cast<double>( duration ), text );
    }

    void add_text( const vec3_t& origin, float duration, const char* text, ... ) {
        return g_memory->call_virtual<void>( this, 60 + 4U, origin, static_cast<double>( duration ), text );
    }

    void add_text( const vec3_t& origin, float duration, int line_offset, const hellcolor& col, const char* text... ) {
        return g_memory->call_virtual<void>( this, 61 + 4U, origin, line_offset, static_cast<double>( duration ), col.Value.x * 255, col.Value.y * 255, col.Value.z * 255, col.Value.w * 255, text );
    }
};

class c_source2_client {
public:
    c_econ_item_system* get_econ_item_system( ) {
        return g_memory->call_virtual<c_econ_item_system*>( this, 128 );
    }

    c_debug_overlay_game_system* get_debug_overlay( ) {
        return g_memory->call_virtual<c_debug_overlay_game_system*>( this, 166U);
    }
};