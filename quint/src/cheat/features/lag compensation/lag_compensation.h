#pragma once
#include <sdk/entity/pawn.h>
#include <sdk/constants.h>
#include <cheat/features/aimbot shared/hitbox.h>
#include <utils/tight_array.h>


constexpr auto BONE_MATRIX_MEMORY_SIZE = sizeof(bone_data_t) * 128;
constexpr float VALID_LAGCOMP_CONE_COSINE = 0.707107f;    // 45 degree angle

struct lag_record_t {

    bool m_visual_only = false;
    bool m_extrapolated = false;

    c_cs_player_pawn* m_pawn = nullptr;
    c_game_scene_node* m_game_scene_node = nullptr;
    c_skeleton_instance* m_skeleton_instance = nullptr;

    bone_data_t m_bones[128];

    float m_simulation_time = 0.0f;

    vec3_t m_rotation, m_origin = {};

    tight_array< c_hitbox_data, 19 > m_all_hitboxes;
    tight_array< c_hitbox_data, 11 > m_rage_hitboxes;

    bool setup(c_cs_player_pawn* pawn);
    bool should_skip_interpolation();
    void try_adjust_velocity();
    bool is_valid();
};

struct extrapolation_data_t {
    vec3_t m_origin{};
    vec3_t m_velocity{};
    vec3_t m_mins{};
    vec3_t m_maxs{};
    uint32_t m_flags{};
    float m_sim_time{};
    float m_direction{};
};

class c_lag_compensation {
public:
    void run();
    void force_input_history();
    bool wants_lag_compensation_on_entity(lag_record_t* victim);
    void predict_movement(extrapolation_data_t& data, c_cs_player_pawn* skip_pawn);
    std::optional<lag_record_t> extrapolate(c_cs_player_pawn* pawn, const lag_record_t& latest, const lag_record_t* prev = nullptr);
};

inline auto g_lagcomp = std::make_unique<c_lag_compensation>();