#pragma once

#include <includes.h>

enum e_clantag_animation : int {
	clantag_anim_static = 0,
	clantag_anim_scroll,
	clantag_anim_reverse_scroll,
	clantag_anim_wave,
	clantag_anim_max
};

class c_clantag {
public:
	void update();
	void reset();
	void invalidate();

	bool is_enabled() const;

private:
	bool can_apply() const;
	void capture_base_name();
	void unlock_name_cvar();
	std::string get_base_name() const;
	std::string strip_tag_from_name(const std::string& name) const;
	void apply_display_name();
	void sync_animation_mode();

	std::string sanitize_text(std::string text) const;
	std::string build_frame() const;
	std::string compose_display_name(const std::string& base_name) const;

	float m_last_update_time = 0.f;
	float m_last_apply_time = 0.f;
	size_t m_anim_index = 0;
	bool m_wave_expanding = true;
	std::string m_base_name;
	bool m_base_name_saved = false;
	int m_cached_animation = -1;
	std::string m_last_applied;
};

inline auto g_clantag = std::make_unique<c_clantag>();
