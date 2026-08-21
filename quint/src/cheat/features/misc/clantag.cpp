#include "clantag.h"

#include <cheat/config/vars.h>
#include <cheat/menu/menu.h>
#include <context.h>
#include <core/interfaces/interfaces.h>
#include <sdk/interfaces/engine_cvar.h>


const std::string CLANTAG_TEXT = "quint cs2";
const float ANIMATION_SPEED = 0.5f; 
const int ANIMATION_TYPE = 2; 

bool c_clantag::is_enabled() const {
	return GET_VAR(bool, MISC_PATH(m_enabled_clantag));
}

bool c_clantag::can_apply() const {
	if (g_menu && g_menu->m_menu_open)
		return false;

	if (!is_enabled())
		return false;

	if (!g_interfaces || !g_interfaces->m_engine || !g_interfaces->m_global_vars)
		return false;

	return g_interfaces->m_engine->is_connected() && g_interfaces->m_engine->in_game();
}


std::string c_clantag::strip_tag_from_name(const std::string& name) const {
	if (name.size() <= CLANTAG_TEXT.size() + 1)
		return name;

	if (name.compare(0, CLANTAG_TEXT.size(), CLANTAG_TEXT) == 0 && name[CLANTAG_TEXT.size()] == ' ')
		return name.substr(CLANTAG_TEXT.size() + 1);

	return name;
}

void c_clantag::capture_base_name() {
	if (m_base_name_saved)
		return;

	if (!g_interfaces || !g_interfaces->m_engine_convar)
		return;

	if (const auto name = g_interfaces->m_engine_convar->find_by_name(xx("name")); name && name->m_value.sz) {
		m_base_name = strip_tag_from_name(name->m_value.sz);
		m_base_name_saved = !m_base_name.empty();
	}
}

std::string c_clantag::get_base_name() const {
	if (!m_base_name.empty())
		return m_base_name;

	if (g_interfaces && g_interfaces->m_engine_convar) {
		if (const auto name = g_interfaces->m_engine_convar->find_by_name(xx("name")); name && name->m_value.sz)
			return strip_tag_from_name(name->m_value.sz);
	}

	return xx("player");
}

void c_clantag::unlock_name_cvar() {
	if (!g_interfaces || !g_interfaces->m_engine_convar)
		return;

	if (const auto name = g_interfaces->m_engine_convar->find_by_name(xx("name")); name) {
		name->m_flags |= FCVAR_USERINFO;
		name->m_flags &= ~FCVAR_PROTECTED;
	}
}

std::string c_clantag::build_frame() const {
	const std::string base = CLANTAG_TEXT;
	if (base.empty())
		return {};

	switch (ANIMATION_TYPE) {
	case 0: // static
		return base;

	case 1: // scroll
	{
		std::string padded = base;
		if (padded.size() < 15)
			padded.append(15 - padded.size(), ' ');

		if (padded.empty())
			return base;

		const size_t shift = m_anim_index % padded.size();
		std::rotate(padded.begin(), padded.begin() + static_cast<std::ptrdiff_t>(shift), padded.end());
		return padded.substr(0, 15);
	}

	case 2: // wave
	{
		const size_t max_len = std::min<size_t>(base.size(), 15);
		if (max_len == 0)
			return base;

		const size_t visible = std::max<size_t>(1, std::min(m_anim_index, max_len));
		return base.substr(0, visible);
	}

	default:
		return base;
	}
}

std::string c_clantag::compose_display_name(const std::string& base_name) const {
	const std::string tag = build_frame();
	if (tag.empty())
		return base_name;

	if (base_name.empty())
		return tag;

	return tag + " " + base_name;
}

void c_clantag::apply_display_name() {
	if (!can_apply())
		return;

	capture_base_name();
	unlock_name_cvar();

	const std::string display_name = compose_display_name(get_base_name());
	if (display_name.empty())
		return;

	if (display_name == m_last_applied)
		return;

	const std::string cmd = std::string(xx("setinfo name \"")) + display_name + '"';
	g_interfaces->m_engine->exec_client_cmd_unrestricted(cmd.c_str());
	m_last_applied = display_name;
}

void c_clantag::invalidate() {
	m_anim_index = 0;
	m_wave_expanding = true;
	m_last_update_time = 0.f;
	m_last_applied.clear();
}

void c_clantag::update() {
	if (!can_apply())
		return;

	capture_base_name();
	unlock_name_cvar();

	const std::string frame = build_frame();
	if (frame.empty()) {
		if (m_base_name_saved)
			reset();
		return;
	}

	const float now = g_interfaces->m_global_vars->m_curtime;

	if (ANIMATION_TYPE != 0) {
		if (now - m_last_update_time >= ANIMATION_SPEED) {
			m_last_update_time = now;

			if (ANIMATION_TYPE == 2) { 
				const size_t max_len = std::min<size_t>(CLANTAG_TEXT.size(), 15);
				if (max_len == 0)
					return;

				if (m_wave_expanding) {
					if (++m_anim_index >= max_len)
						m_wave_expanding = false;
				}
				else if (m_anim_index == 0) {
					m_wave_expanding = true;
				}
				else {
					--m_anim_index;
				}
			}
			else {
				++m_anim_index;
			}

			m_last_applied.clear();
		}
	}

	if (now - m_last_apply_time >= 0.5f || m_last_applied.empty())
		apply_display_name();

	m_last_apply_time = now;
}

void c_clantag::reset() {
	const std::string restore_name = m_base_name.empty() ? get_base_name() : m_base_name;

	m_base_name_saved = false;
	m_base_name.clear();
	m_anim_index = 0;
	m_wave_expanding = true;
	m_last_update_time = 0.f;
	m_last_apply_time = 0.f;
	m_last_applied.clear();

	if (!g_interfaces || !g_interfaces->m_engine)
		return;

	if (!restore_name.empty()) {
		const std::string cmd = std::string(xx("setinfo name \"")) + restore_name + '"';
		g_interfaces->m_engine->exec_client_cmd_unrestricted(cmd.c_str());
	}
}