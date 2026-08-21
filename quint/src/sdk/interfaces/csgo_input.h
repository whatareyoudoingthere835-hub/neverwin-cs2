#pragma once

#include <includes.h>
#include <sdk/datatypes/utl_vector.h>

class c_cs_input_message {
public:
    int32_t m_render_tick_count; //0x0000
    float m_render_tick_fraction; //0x0004
    int32_t m_player_tick_count; //0x0008
    float m_player_tick_fraction; //0x000C
    vec3_t m_view_angles; //0x0010
    vec3_t m_origin; //0x001C
    char pad_0028[ 56 ]; //0x0028
}; //Size: 0x0060

class c_button_data {
public:
    std::uint64_t m_flag;
    char* m_name;
    std::byte pad0[ 0x8 ];
    float m_value;
    std::byte pad1[ 0x4 ];
    float m_when_start;
    float m_when_end;
    std::byte pad2[ 0x4 ];
    int m_command_count;
    std::byte pad3[ 0x60 ];
}; //Size: 0x0048

class c_subtick_input {
public:
    float m_when; //0x0000
    float m_delta; //0x0004
    uint64_t m_button; //0x0008
    bool m_pressed; //0x0010
    char pad_0011[ 3 ]; //0x0011
    int m_mouse_input;
    vec3_t m_view_angle;
};

class c_csgo_input {
public:
    char pad_0000[16];
    c_button_data* m_buttons_data[63];
    char pad_0208[72];
    bool m_block_shot;
    bool m_in_thirdperson;
    char pad_0252[6];
    vec3_t m_third_person_angles;
    char pad_0264[20];
    uint64_t m_button_pressed;
    uint64_t m_mouse_button_pressed;
    uint64_t m_button_un_pressed;
    uint64_t m_keyboard_copy;
    float m_forward_move;
    float m_left_move;
    float m_up_move;
    vec2_t m_mouse_pos;
    int32_t m_subtick_count;
    c_subtick_input m_subticks[32];
    vec3_t m_view_angles;
    int32_t m_target_entity_index;
    char pad_03E0[152];
    int32_t m_max_subtick_moves;
    char pad_047C[4];
    float m_last_button_frame_number;
    char pad_0484[4];
    int32_t m_last_button_presed;
    char pad_048C[4];
    bool m_in_move;
    char pad_0491[7];
    float m_time_since_move;
    char pad_049C[4];
    int32_t m_max_button;
    char pad_04A4[4];
    bool m_some_active;
    char pad_04A9[356];
    bool m_in_attack;
    bool m_in_attack2;
    bool m_in_attack3;
    int32_t m_attack_history_1;
    int32_t m_attack_history_2;
    int32_t m_attack_history_3;
    char pad_061C[4];
    int32_t m_message_size;
    char pad_0624[4];
    c_utl_vector<c_cs_input_message> m_message;

    void add_button( uint64_t button ) {
        if ( !( m_button_pressed & button ) )
            m_button_pressed |= button;
    }

    void add_subtick_button( uint64_t button ) {
        auto& ref = m_subticks[ m_subtick_count ];
        ref.m_pressed = true;
        ref.m_button = button;
    }

    void set_view_angle( const vec3_t angView ) {
        // dwViewAngles из дампа build 14176.
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_modules->m_client.get());
        *reinterpret_cast<vec3_t*>(base + 0x23C01A8) = angView;
    }

    vec3_t get_view_angle( ) {
        // dwViewAngles из дампа build 14176.
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_modules->m_client.get());
        return *reinterpret_cast<vec3_t*>(base + 0x23C01A8);
    }

};