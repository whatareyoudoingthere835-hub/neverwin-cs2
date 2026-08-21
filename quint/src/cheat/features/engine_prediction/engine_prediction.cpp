#include "engine_prediction.h"

#include <core/interfaces/interfaces.h>
#include <sdk/interfaces/global_variables.h>







void c_engine_prediction::begin() {
    if (g_ctx->m_cmd->m_sequence_number == this->m_last_sequence_processed)
        return;

    c_player_movement_services* movement_services = g_ctx->m_local_pawn->m_pMovementServices();
    if (!movement_services)
        return;

    c_network_game_client* network_game_client = g_interfaces->m_network_client_services->get_network_game_client();
    if (!network_game_client)
        return;

    c_user_cmd_manager* user_cmd_manager = g_ctx->m_local_controller->get_cmd_manager();
    if (!user_cmd_manager)
        return;

    m_prediction_data.m_pre_prediction_flags = g_ctx->m_local_pawn->m_fFlags();
    m_prediction_data.m_abs_velocity = g_ctx->m_local_pawn->m_vecAbsVelocity();
    m_prediction_data.m_velocity = g_ctx->m_local_pawn->m_vecVelocity();



    m_prediction_data.m_frame_time = g_interfaces->m_global_vars->m_frame_time;
    m_prediction_data.m_frame_time2 = g_interfaces->m_global_vars->m_frame_advance;
    m_prediction_data.m_curtime = g_interfaces->m_global_vars->m_curtime;

    m_prediction_data.m_client_tick_fraction = g_interfaces->m_global_vars->m_client_tick_fraction;
    m_prediction_data.m_next_tick_fraction = g_interfaces->m_global_vars->m_next_tick_fraction;
    m_prediction_data.m_tick_count = g_interfaces->m_global_vars->m_tick_count;

    m_prediction_data.m_in_prediction = g_interfaces->m_prediction->m_in_prediction;
    m_prediction_data.m_first_prediction = g_interfaces->m_prediction->m_first_prediction;
    m_prediction_data.m_has_been_predicted = g_ctx->m_cmd->m_has_been_predicted;
    m_prediction_data.m_should_predict = network_game_client->m_should_predict;
    g_ctx->m_active_weapon->update_accuracy_penalty();
    m_prediction_data.m_spread = g_ctx->m_active_weapon->get_spread();
    m_prediction_data.m_inaccuracy = g_ctx->m_active_weapon->get_inaccuracy();

    

    g_ctx->m_cmd->m_has_been_predicted = false;
    network_game_client->m_should_predict = true;
    g_interfaces->m_prediction->m_first_prediction = false;
    g_interfaces->m_prediction->m_in_prediction = true;

    movement_services->set_prediction_command(g_ctx->m_cmd);
    static auto fn_physics_run_think = reinterpret_cast<void* (__fastcall*)(void*)>(
        g_modules->m_client.find(xx("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 8B 81 ? ? ? ? 48 8B F9")).as<void*>()
        );
    fn_physics_run_think(g_ctx->m_local_controller);
    //    movement_services->run_command(g_ctx->m_cmd); 


    using fn_calculate_shoot_pos_t = void(__fastcall*)(vec3_t*, c_cs_player_pawn*, timestamp_t*, void*, void*);
    static auto fn_calculate_shoot_pos = g_modules->m_client
        .find(xx("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC ? 44 8B 92"))
        .as<fn_calculate_shoot_pos_t>();

    vec3_t shoot_pos = g_ctx->m_local_pawn->get_eye_pos();
   timestamp_t ts{ g_ctx->m_local_controller->m_nTickBase(), 0.f };
   fn_calculate_shoot_pos(&shoot_pos, g_ctx->m_local_pawn, &ts, nullptr, nullptr);
    g_ctx->m_shoot_position = shoot_pos;
    m_prediction_data.m_post_prediction_flags = g_ctx->m_local_pawn->m_fFlags();

    movement_services->reset_prediction_command();

    g_ctx->m_base->set_client_tick(g_ctx->m_local_controller->m_nTickBase());

}

void c_engine_prediction::end() {

    if (g_ctx->m_cmd->m_sequence_number == this->m_last_sequence_processed)
        return;

    c_player_movement_services* movement_services = g_ctx->m_local_pawn->m_pMovementServices();
    if (!movement_services)
        return;

    c_network_game_client* network_game_client = g_interfaces->m_network_client_services->get_network_game_client();
    if (!network_game_client)
        return;

 

    g_interfaces->m_global_vars->m_frame_time = m_prediction_data.m_frame_time;
    g_interfaces->m_global_vars->m_frame_advance = m_prediction_data.m_frame_time2;
    g_interfaces->m_global_vars->m_curtime = m_prediction_data.m_curtime;

    g_interfaces->m_global_vars->m_client_tick_fraction = m_prediction_data.m_client_tick_fraction;
    g_interfaces->m_global_vars->m_next_tick_fraction = m_prediction_data.m_next_tick_fraction;
    g_interfaces->m_global_vars->m_tick_count = m_prediction_data.m_tick_count;
    
    g_interfaces->m_prediction->m_in_prediction = m_prediction_data.m_in_prediction;
    g_interfaces->m_prediction->m_first_prediction = m_prediction_data.m_first_prediction;
   
    g_ctx->m_cmd->m_has_been_predicted = m_prediction_data.m_has_been_predicted;

    network_game_client->m_should_predict = m_prediction_data.m_should_predict;

    this->m_last_sequence_processed = g_ctx->m_cmd->m_sequence_number;
}