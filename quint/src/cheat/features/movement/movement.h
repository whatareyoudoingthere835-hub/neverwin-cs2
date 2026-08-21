#pragma once

#include <cheat/features/entity cache/entity_cache.h>
#include <context.h>
#include <sdk/interfaces/csgo_input.h>

class c_movement
{
public:
	void bunny_hop();
	void auto_strafe();

	void quick_stop();
	void stop_movement();
	void correct_movement();

	void slow_walk();
	void straight_throw();



	void air_strafer(vec3_t& velocity, float frame_time, vec3_t move);
	void air_accelerate(vec3_t angles, vec3_t& velocity, vec3_t& move, float frame_time);
	void subtick_air_strafer(bool stop);



	vec3_t m_camera_angle;
	vec3_t m_base_angle;

	struct auto_peek_render_state_t {
		bool retreating = false;
		bool active = false;
		vec3_t origin{};
	};

	auto_peek_render_state_t g_auto_peek_render_state;
	std::deque<vec3_t> g_auto_peek_origin = { };
	bool g_auto_peek_returning = false;

	void auto_peek();

	bool wants_stop{};

	void auto_stop(bool want);
	void jumpscout(bool want);






	
};

inline auto g_movement = std::make_unique<c_movement>();