#include "overlay_features.h"
#include "../src/sdk/interfaces/global_variables.h"
#include "../src/core/interfaces/interfaces.h"
#include <context.h>
#include "cheat/features/entity cache/entity_cache.h"
#include <sdk/entity/bomb.h>
#include <cheat/config/vars.h>
#include <cheat/menu/menu.h>
#include <cheat/features/visuals/grenade.h>
#include <cheat/menu/hell_gui/blur.h>
#include <sdk/entity/pawn.h>
#include <algorithm>
#include <d3d11.h>
#include <cheat/menu/hell_gui/hell_gui.h>
#include <sdk/interfaces/csgo_input.h>
#include <math/math.h>
#include <array>
#include <sdk/constants.h>
#include <cheat/menu/hell_gui/colors.h>
#include <cheat/menu/tabs/aimbot_tab.h>
#include <cheat/features/ragebot/shotlogger.h>
#include <cheat/features/ragebot/ragebot.h>
#include <chrono>
#include <cheat/input.h>

void c_overlay::draw_world_damage_markers() {
    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;

    if (!g_interfaces->m_global_vars)
        return;

    if (!GET_VAR(bool, VISUALS_PATH(m_enabled_3d_damage_markers)))
        return;

    const float current_time = g_interfaces->m_global_vars->m_curtime;

    for (int i = m_world_damage_markers.size() - 1; i >= 0; --i) {
        auto& marker = m_world_damage_markers[i];

        float time_alive = current_time - marker.m_spawn_time;
        if (time_alive > marker.m_lifetime) {
            m_world_damage_markers.erase(m_world_damage_markers.begin() + i);
            continue;
        }

        vec3_t pos = damage_pos + vec3_t(0, 0, time_alive);
        vec2_t screen_pos_vector;
        if (!g_math->world_to_screen(pos, screen_pos_vector))
            continue;

        hellvec2 screen_pos_hit_marker = { screen_pos_vector.x, screen_pos_vector.y + 16.5f };
        hellvec2 screen_pos_default = { screen_pos_vector.x, screen_pos_vector.y };

        float fade_in_duration = 0.20f;
        float fade_out_start = marker.m_lifetime * 0.7f;
        float alpha = 1.0f;

        if (time_alive < fade_in_duration) {
            float t = time_alive / fade_in_duration;
            alpha = t * t;
        }
        else if (time_alive > fade_out_start) {
            float t = (time_alive - fade_out_start) / (marker.m_lifetime - fade_out_start);
            alpha = 1.0f - (t * t);
        }

        static c_visuals::text_object_t text_object{};
        text_object.m_fg_color = GET_VAR(hellcolor, VISUALS_PATH(m_enabled_3d_damage_markers_color));
        text_object.m_bg_color = GET_VAR(hellcolor, VISUALS_PATH(m_enabled_3d_damage_markers_color_bg));
        text_object.m_font_type = (c_visuals::e_font_type)(GET_VAR(int, VISUALS_PATH(m_3d_damage_markers_font_type)));
        text_object.m_text_shadow_type = (c_visuals::e_text_shadow_type)(GET_VAR(int, VISUALS_PATH(m_3d_damage_markers_shadow_type)));

        std::string damage_str = std::to_string(marker.m_damage);
        text_object.m_text = damage_str.c_str();
        if (GET_VAR(bool, VISUALS_PATH(m_enabled_3d_hitmarkers)))
            g_visuals->draw_text(c_visuals::bb_t{ screen_pos_hit_marker, screen_pos_hit_marker }, text_object, alpha, ImGui::GetBackgroundDrawList());
        else
            g_visuals->draw_text(c_visuals::bb_t{ screen_pos_default, screen_pos_default }, text_object, alpha, ImGui::GetBackgroundDrawList());
    }
}

void c_overlay::add_hitmarker(const vec3_t& world_pos) {
    if (!g_interfaces->m_global_vars)
        return;

    hit_marker_t marker{};
    marker.m_world_origin = world_pos;
    marker.m_spawn_time = g_interfaces->m_global_vars->m_curtime;
    marker.m_lifetime = 1.8f;
    m_hit_markers.push_back(marker);
}

void c_overlay::draw_hitmarkers() {
    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;

    if (!g_interfaces->m_global_vars)
        return;

    if (!GET_VAR(bool, VISUALS_PATH(m_enabled_3d_hitmarkers)))
        return;

    const float current_time = g_interfaces->m_global_vars->m_curtime;
    auto* draw_list = ImGui::GetForegroundDrawList();

    const float max_size = 8.0f;
    const float base_thickness = 1.5f;
    const float gap = 3.0f;
    const hellcolor base_color = GET_VAR(hellcolor, VISUALS_PATH(m_enabled_3d_hitmarkers_color));

    auto draw_gradient_line_3d = [&](const ImVec2& start, const ImVec2& end, const hellcolor& color_start, const hellcolor& color_end, float thickness, float anim_progress, int corner_type) {
        ImVec2 anim_start = start;
        ImVec2 anim_end = end;

        if (anim_progress < 1.0f) {
            float anim_offset = (max_size + gap) * 2.0f;
            ImVec2 corner_offset;
            if (corner_type == 0) {
                corner_offset = ImVec2(-anim_offset, -anim_offset);
            }
            else if (corner_type == 1) {
                corner_offset = ImVec2(-anim_offset, anim_offset);
            }
            else if (corner_type == 2) {
                corner_offset = ImVec2(anim_offset, -anim_offset);
            }
            else {
                corner_offset = ImVec2(anim_offset, anim_offset);
            }

            float ease = 1.0f - (1.0f - anim_progress) * (1.0f - anim_progress) * (1.0f - anim_progress);
            anim_start = ImVec2(
                start.x + corner_offset.x * (1.0f - ease),
                start.y + corner_offset.y * (1.0f - ease)
            );
            anim_end = ImVec2(
                end.x + corner_offset.x * (1.0f - ease),
                end.y + corner_offset.y * (1.0f - ease)
            );
        }

        const int segments = 8;
        for (int i = 0; i < segments; ++i) {
            float t1 = (float)i / (float)segments;
            float t2 = (float)(i + 1) / (float)segments;

            ImVec2 p1 = ImVec2(anim_start.x + (anim_end.x - anim_start.x) * t1, anim_start.y + (anim_end.y - anim_start.y) * t1);
            ImVec2 p2 = ImVec2(anim_start.x + (anim_end.x - anim_start.x) * t2, anim_start.y + (anim_end.y - anim_start.y) * t2);

            hellcolor segment_color = hellcolor(
                color_start.Value.x + (color_end.Value.x - color_start.Value.x) * t1,
                color_start.Value.y + (color_end.Value.y - color_start.Value.y) * t1,
                color_start.Value.z + (color_end.Value.z - color_start.Value.z) * t1,
                color_start.Value.w + (color_end.Value.w - color_start.Value.w) * t1
            );

            draw_list->AddLine(p1, p2, segment_color, thickness);
        }
        };

    for (std::size_t i = 0; i < m_hit_markers.size(); ) {
        auto& marker = m_hit_markers[i];
        const float time_alive = current_time - marker.m_spawn_time;

        if (time_alive > marker.m_lifetime) {
            m_hit_markers.erase(m_hit_markers.begin() + i);
            continue;
        }

        vec2_t screen;
        if (!g_math->world_to_screen(marker.m_world_origin, screen)) {
            ++i;
            continue;
        }

        const float alpha = 1.f - (time_alive / marker.m_lifetime);
        const float anim_duration = 0.15f;
        const float anim_progress = std::min(1.0f, time_alive / anim_duration);
        const hellvec2 center{ screen.x, screen.y };

        hellcolor core = base_color;
        core.Value.w = alpha;

        hellcolor fade_color = core;
        fade_color.Value.w = 0.0f;

        hellcolor glow_color = core;
        glow_color.Value.w = alpha * 0.3f;
        hellcolor glow_fade = glow_color;
        glow_fade.Value.w = 0.0f;

        float glow_radius = (max_size + gap) * 1.5f;

        const int glow_segments = 64;
        const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;
        const unsigned int vtx_base = draw_list->_VtxCurrentIdx;

        draw_list->PrimReserve(glow_segments * 3, glow_segments + 1);
        draw_list->PrimWriteVtx(center, uv, glow_color);

        for (int j = 0; j < glow_segments; ++j) {
            float angle = (float)j / (float)glow_segments * 2.0f * IM_PI;
            ImVec2 pos = ImVec2(center.x + cosf(angle) * glow_radius, center.y + sinf(angle) * glow_radius);
            draw_list->PrimWriteVtx(pos, uv, glow_fade);
        }

        for (int j = 0; j < glow_segments; ++j) {
            draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base));
            draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + j));
            draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + ((j + 1) % glow_segments)));
        }

        draw_gradient_line_3d(
            ImVec2(center.x - gap - max_size, center.y - gap - max_size),
            ImVec2(center.x - gap, center.y - gap),
            fade_color, core, base_thickness, anim_progress, 0
        );

        draw_gradient_line_3d(
            ImVec2(center.x - gap - max_size, center.y + gap + max_size),
            ImVec2(center.x - gap, center.y + gap),
            fade_color, core, base_thickness, anim_progress, 1
        );

        draw_gradient_line_3d(
            ImVec2(center.x + gap + max_size, center.y - gap - max_size),
            ImVec2(center.x + gap, center.y - gap),
            fade_color, core, base_thickness, anim_progress, 2
        );

        draw_gradient_line_3d(
            ImVec2(center.x + gap + max_size, center.y + gap + max_size),
            ImVec2(center.x + gap, center.y + gap),
            fade_color, core, base_thickness, anim_progress, 3
        );

        ++i;
    }
}


void c_overlay::clear_hitmarks() {
    m_hit_markers.clear();
}

void c_overlay::spread_circle() {
    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;

    const auto local = g_ctx->m_local_pawn;
    if (!local || !local->is_alive())
        return;

    if (!GET_VAR(bool, VISUALS_PATH(m_enabled_spread_circle)))
        return;

    const auto cmd = g_ctx->m_cmd;
    const auto base_cmd = g_ctx->m_base;
    const auto weapon = g_ctx->m_active_weapon;
    if (!cmd || !base_cmd || !weapon)
        return;

    const auto& io = ImGui::GetIO();
    const hellvec2 center = ImGui::GetMainViewport()->GetCenter();

    static float s_radius = 0.f;
    static float s_scale = 2.f;

    float inaccuracy = weapon->get_inaccuracy();
    float spread = weapon->get_spread();
    float velocity_factor = g_ctx->m_local_pawn->m_vecAbsVelocity().length_2d();

    if (velocity_factor < 5.0f) {
        velocity_factor = 1.0f;
    }
    else {
        velocity_factor = velocity_factor / 250.0f;
    }

    const float target = (inaccuracy + spread) * velocity_factor * (io.DisplaySize.y / s_scale / 2.f);
    s_radius += (target - s_radius) * 0.15f;

    const auto col1 = ImGui::ColorConvertFloat4ToU32(GET_VAR(hellcolor, VISUALS_PATH(m_enabled_spread_circle_color_first)));
    const auto col2 = ImGui::ColorConvertFloat4ToU32(GET_VAR(hellcolor, VISUALS_PATH(m_enabled_spread_circle_color_last)));
    const auto uv = io.Fonts->TexUvWhitePixel;

    auto* dl = ImGui::GetBackgroundDrawList();
    constexpr int segs = 200;
    constexpr float step = 2.f * IM_PI / segs;
    const int base = dl->VtxBuffer.Size;

    dl->PrimReserve(segs * 3, segs + 1);
    dl->PrimWriteVtx(center, uv, col1);

    for (int i = 0; i < segs; ++i) {
        float a = i * step;
        dl->PrimWriteVtx({ center.x + std::cos(a) * s_radius, center.y + std::sin(a) * s_radius }, uv, col2);
    }

    for (int i = 0; i < segs; ++i) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(base + 1 + i);
        dl->PrimWriteIdx(base + 1 + ((i + 1) % segs));
    }
}

static void draw_radial_gradient(ImDrawList* draw_list, const hellvec2& center, float radius, ImU32 col_inner, ImU32 col_outer, int segments = 64) {
    if (((col_inner | col_outer) & IM_COL32_A_MASK) == 0 || radius < 0.5f)
        return;

    const float angle_step = 2.0f * IM_PI / segments;
    const hellvec2 uv = draw_list->_Data->TexUvWhitePixel;

    const unsigned int vtx_base = draw_list->_VtxCurrentIdx;
    draw_list->PrimReserve(segments * 3, segments + 1);


    draw_list->PrimWriteVtx(center, uv, col_inner);

    for (int i = 0; i < segments; ++i) {
        float angle = i * angle_step;
        float x = center.x + std::cos(angle) * radius;
        float y = center.y + std::sin(angle) * radius;
        draw_list->PrimWriteVtx(hellvec2(x, y), uv, col_outer);
    }

    for (int i = 0; i < segments; ++i) {
        draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base));
        draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + i));
        draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + ((i + 1) % segments)));
    }
}

void c_overlay::push_hitbox(const c_hitbox_data& hitbox, ImU32 color, int segments) {
    m_capsules.push_back({ hitbox.m_mins, hitbox.m_maxs, hitbox.m_radius, color, segments });
}

void c_overlay::render_all() {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    for (const auto& cap : m_capsules) {
        const vec3_t axis = (cap.m_end - cap.m_start).normalized();

        vec3_t right = axis.cross(vec3_t(0, 0, 1));
        if (right.length_sqr() < 1e-3f)
            right = vec3_t(1, 0, 0);
        right.normalize();

        vec3_t up = axis.cross(right).normalized();

        constexpr int rings = 30;
        constexpr float fade_outer = 20.0f;

        for (int i = 0; i <= rings; ++i) {
            float t = (float)i / (float)rings;

            vec3_t ring_center = cap.m_start + (cap.m_end - cap.m_start) * t;

            vec2_t screen_center;
            if (!g_math->world_to_screen(ring_center, screen_center))
                continue;

            const hellvec2 center_2d(screen_center.x, screen_center.y);

            vec3_t offset_pos = ring_center + right * cap.m_radius;

            vec2_t screen_offset;
            if (!g_math->world_to_screen(offset_pos, screen_offset))
                continue;

            float visual_radius = (screen_offset - screen_center).length();

            hellcolor base(cap.m_color);
            hellcolor outer_color(base.Value.x, base.Value.y, base.Value.z, 0.0f);
            const ImU32 inner = cap.m_color;

            draw_radial_gradient(draw, center_2d, visual_radius, inner, outer_color);
        }
    }
}

void c_overlay::clear() {
    m_capsules.clear();
}

void circle_progress_bomb(ImDrawList* pDrawList, const ImVec2& center, float radius, const hellcolor& color, float thickness, float progress, float start_angle) {
    if (progress <= 0.0f) return;

    const float end_angle = start_angle + (IM_PI * 2.0f * progress);
    const int num_segments = 32;

    pDrawList->PathArcTo(ImVec2(center.x, center.y), radius, start_angle, end_angle, num_segments);
    pDrawList->PathStroke(color, 0, thickness);
}

void c_overlay::draw_planted_bomb_world()
{
	bool bShowIcon = GET_VAR(bool, VISUALS_PATH(m_planted_bomb_world_icon));
	bool bShowTimer = GET_VAR(bool, VISUALS_PATH(m_planted_bomb_world_timer));

	if (!bShowIcon && !bShowTimer)
		return;

	if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
		return;

	if (!g_ctx->m_local_pawn)
		return;

	if (!g_ctx->m_local_controller)
		return;

	if (!g_ctx->m_local_controller->m_bPawnIsAlive())
		return;

	C_PlantedC4* planted = bomb::get_planted_bomb();
	if (!planted || !planted->m_bBombTicking())
		return;

	int nMaxDistance = GET_VAR(int, VISUALS_PATH(m_planted_bomb_distance));
	vec3_t vLocalPos = g_ctx->m_local_pawn->get_world_space_center();

	c_game_scene_node* pGameSceneNode = planted->m_pGameSceneNode();
	if (!pGameSceneNode)
		return;

	vec3_t vBombPos = pGameSceneNode->m_vecAbsOrigin();
	float flDistance = vLocalPos.dist_to(vBombPos);

	if (flDistance > nMaxDistance)
		return;

	vec2_t vScreen;
	if (!g_math->world_to_screen(vBombPos, vScreen))
		return;

	float flAlpha = 1.0f;
	if (flDistance > 200.0f) {
		flAlpha = std::max(0.0f, 1.0f - (flDistance - 200.0f) / (nMaxDistance - 200.0f));
	}

	if (flAlpha < 0.01f)
		return;

	float time_left = bomb::get_time_to_explode();
	if (time_left <= 0.0f)
		return;

	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	float flCurrentY = vScreen.y;

	if (bShowTimer) {
		hellcolor timerColor = GET_VAR(hellcolor, VISUALS_PATH(m_planted_bomb_timer_color));
		timerColor.Value.w *= flAlpha;
		hellcolor timerBgColor = GET_VAR(hellcolor, VISUALS_PATH(m_planted_bomb_timer_color_bg));
		timerBgColor.Value.w *= flAlpha;

		int nFontType = GET_VAR(int, VISUALS_PATH(m_planted_bomb_timer_font_type));
		int nShadowType = GET_VAR(int, VISUALS_PATH(m_planted_bomb_timer_shadow_type));

		c_visuals::e_font_type font_type = static_cast<c_visuals::e_font_type>(nFontType);
		ImFont* pFont = g_visuals->get_font(font_type);
		if (!pFont)
			return;

		char szTimer[32];
		int seconds = static_cast<int>(time_left);
		int milliseconds = static_cast<int>((time_left - seconds) * 10.0f);
		std::snprintf(szTimer, sizeof(szTimer), "%d.%d", seconds, milliseconds);

		float fontSize = pFont->LegacySize;
		ImVec2 textSize = pFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, szTimer);

		ImVec2 textPos = ImVec2(vScreen.x - textSize.x * 0.5f, flCurrentY);

		if (nShadowType == c_visuals::e_text_shadow_type::text_shadow_full) {
			for (int x = -1; x <= 1; x++) {
				for (int y = -1; y <= 1; y++) {
					if (x == 0 && y == 0)
						continue;
					pDrawList->AddText(pFont, fontSize, ImVec2(textPos.x + x, textPos.y + y), timerBgColor, szTimer);
				}
			}
		}
		else if (nShadowType == c_visuals::e_text_shadow_type::text_shadow_drop) {
			hellcolor shadowColor = timerBgColor;
			shadowColor.Value.x = 0.0f;
			shadowColor.Value.y = 0.0f;
			shadowColor.Value.z = 0.0f;
			pDrawList->AddText(pFont, fontSize, ImVec2(textPos.x + 1, textPos.y + 1), shadowColor, szTimer);
		}

		pDrawList->AddText(pFont, fontSize, textPos, timerColor, szTimer);
		
		flCurrentY = textPos.y - 2.0f;
	}

	if (bShowIcon) {
		static icon_data_t bomb_icon = get_panorama_texture("icons/equipment/c4");
		
		if (bomb_icon.texture_view && bomb_icon.width > 0 && bomb_icon.height > 0) {
			int iTargetSize = 12;
			const auto flWtoHRatio = static_cast<float>(bomb_icon.width) / static_cast<float>(bomb_icon.height);
			auto Width = static_cast<uint32_t>(flWtoHRatio * iTargetSize);
			auto Height = iTargetSize;
			
			ImVec2 vIconPos = ImVec2(vScreen.x - Width * 0.5f, flCurrentY - Height);
			
			hellcolor iconColor = GET_VAR(hellcolor, VISUALS_PATH(m_planted_bomb_icon_color));
			iconColor.Value.w *= flAlpha;
			pDrawList->AddImage(bomb_icon.texture_view, vIconPos, ImVec2(vIconPos.x + Width, vIconPos.y + Height), ImVec2(0, 0), ImVec2(1, 1), iconColor);
		}
	}
}

void c_overlay::visualize_aimbot() {
    if (!GET_VAR(bool, VISUALS_PATH(m_visualize_hitboxes)))
        return;

    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game() || !g_ctx->m_local_pawn || !g_ctx->m_local_pawn->is_alive())
        return;

    if (!g_ctx->m_active_weapon)
        return;

    vec3_t shoot_position = g_ctx->m_shoot_position;

    int weapon_rage_type = get_ragebot_weapon_type(g_ctx->m_weapon_def_index);
    if (weapon_rage_type == 10)
        return;

    fnv1a_t dmg_holder_id = tabs::aimbot::get_min_damage_holder_id(weapon_rage_type);
    int min_damage_setting = GET_VAR(int, dmg_holder_id);

    if (min_damage_setting <= 0)
        return;

    for (auto& player : g_entity_cache->m_players) {
        if (player.m_lag_records.empty() || player.m_pawn == g_ctx->m_local_pawn)
            continue;

        auto& record = player.m_lag_records.newest();

        int target_health = record.m_pawn ? record.m_pawn->m_iHealth() : 0;
        int dmg_threshold = min_damage_setting;
        if (dmg_threshold > target_health)
            dmg_threshold = target_health;
        else if (dmg_threshold > 100)
            dmg_threshold = target_health + min_damage_setting - 100;

        auto& penetration_ctx = player.m_penetration_context;
        penetration_ctx.fill(record.m_pawn);

        c_handle_bullet_penetration_data pen_data;
        pen_data.m_damage = m_local_context.m_damage;
        pen_data.m_penetration = m_local_context.m_penetration;
        pen_data.m_range_modifier = m_local_context.m_range_mod;
        pen_data.m_pen_count = 4;

        for (auto& hitbox : record.m_rage_hitboxes) {
            int current_damage = 0;
            if (penetration_ctx.fire_bullet(shoot_position, hitbox.m_center, record.m_pawn, pen_data))
                current_damage = static_cast<int>(pen_data.m_damage);

            bool can_damage_current = current_damage >= dmg_threshold;
            bool predictive_damage = can_damage_current;

            if (!predictive_damage) {
                std::vector<vec3_t> positions_to_check = {
                    shoot_position + vec3_t(32.0f, 0.0f, 0.0f),
                    shoot_position + vec3_t(-32.0f, 0.0f, 0.0f),
                    shoot_position + vec3_t(0.0f, 32.0f, 0.0f),
                    shoot_position + vec3_t(0.0f, -32.0f, 0.0f),
                    shoot_position + vec3_t(0.0f, 0.0f, 24.0f),
                    shoot_position + vec3_t(0.0f, 0.0f, -24.0f),
                    shoot_position + vec3_t(24.0f, 24.0f, 0.0f),
                    shoot_position + vec3_t(-24.0f, 24.0f, 0.0f),
                    shoot_position + vec3_t(24.0f, -24.0f, 0.0f),
                    shoot_position + vec3_t(-24.0f, -24.0f, 0.0f)
                };

                for (vec3_t pos : positions_to_check) {
                    int damage = 0;
                    if (penetration_ctx.fire_bullet(shoot_position, hitbox.m_center, record.m_pawn, pen_data))
                        damage = static_cast<int>(pen_data.m_damage);

                    if (damage >= dmg_threshold) {
                        predictive_damage = true;
                        break;
                    }
                }
            }

            if (!predictive_damage)
                continue;

            hellcolor base_color = GET_VAR(hellcolor, VISUALS_PATH(m_visualize_hitbox_color));
            ImU32 render_color = ImGui::ColorConvertFloat4ToU32(base_color.Value);
            g_overlay->push_hitbox(hitbox, render_color, 5);
        }
    }
}

void c_overlay::radial_gradient(ImDrawList* draw_list, const vec3_t& world_center, float radius, ImU32 center_color, ImU32 edge_color) {
    if (((center_color | edge_color) & IM_COL32_A_MASK) == 0 || radius < 0.5f)
        return;

    constexpr int count = 100;

    vec2_t center_screen;
    if (!g_math->world_to_screen(world_center, center_screen))
        return;

    std::vector<hellvec2> screen_vertices;
    screen_vertices.reserve(count + 1);

    screen_vertices.emplace_back(center_screen.x, center_screen.y);

    constexpr float pi_f = static_cast<float>(A_PI);
    for (int i = 0; i < count; ++i) {
        const float angle = 2.0f * pi_f * i / count;
        const float x = radius * std::cos(angle);
        const float y = radius * std::sin(angle);

        vec3_t world_point = world_center + vec3_t(x, y, 0.0f);
        vec2_t screen_point;

        if (g_math->world_to_screen(world_point, screen_point)) {
            screen_vertices.emplace_back(screen_point.x, screen_point.y);
        }
    }

    const int actual_count = static_cast<int>(screen_vertices.size()) - 1;
    if (actual_count < 3)
        return;

    const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;
    const unsigned int vtx_base = draw_list->_VtxCurrentIdx;

    draw_list->PrimReserve(actual_count * 3, actual_count + 1);
    draw_list->PrimWriteVtx(ImVec2(screen_vertices[0].x, screen_vertices[0].y), uv, center_color);

    for (int i = 1; i <= actual_count; ++i) {
        draw_list->PrimWriteVtx(ImVec2(screen_vertices[i].x, screen_vertices[i].y), uv, edge_color);
    }

    for (int i = 0; i < actual_count; ++i) {
        draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(vtx_base));
        draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(vtx_base + 1 + i));
        draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(vtx_base + 1 + ((i + 1) % actual_count)));
    }
}

static ImVec2 RotateVertex(const ImVec2& p, const ImVec2& v, float angle) {
    float c = std::cos(DEG2RAD(angle));
    float s = std::sin(DEG2RAD(angle));
    return {
        p.x + (v.x - p.x) * c - (v.y - p.y) * s,
        p.y + (v.x - p.x) * s + (v.y - p.y) * c
    };
}

void draw_oof(c_cs_player_pawn* pPawn) {
    if (!pPawn || !pPawn->m_pGameSceneNode())
        return;

    if (!GET_VAR(bool, VISUALS_PATH(m_bEnableOOFArrows)))
        return;

    if (!g_ctx->m_local_pawn || !g_ctx->m_local_pawn->is_alive())
        return;

    hellcolor triangleColor = GET_VAR(hellcolor, VISUALS_PATH(m_colOOFArrows));

    float baseSize = 8.0f;
    float screenDistance = 120.0f;

    vec3_t vecViewOrigin, vecTargetPos, vecDelta;
    vec2_t vecScreenPos;
    vec3_t vecOffScreenPos;
    float flLeeWayX, flLeeWayY, flRadius, flOffScreenRotation;
    bool bIsOnScreen;
    ImVec2 vP1, vP2, vP3, vBackCenter, vArrowScreenPos;

    auto RotatePoint = [](const ImVec2& vPoint, const ImVec2& vCenter, float flDeg) -> ImVec2 {
        flDeg = DEG2RAD(flDeg);
        const auto flCos = cosf(flDeg);
        const auto flSin = sinf(flDeg);
        ImVec2 vReturn = ImVec2();
        vReturn.x = flCos * (vPoint.x - vCenter.x) - flSin * (vPoint.y - vCenter.y);
        vReturn.y = flSin * (vPoint.x - vCenter.x) + flCos * (vPoint.y - vCenter.y);
        vReturn += vCenter;
        return vReturn;
        };

    auto GetOffScreenData = [](const vec3_t& vecDelta, float flRadiusX, float flRadiusY, vec3_t& vecOutOffScreenPos, float& flOutRot) {
        vec3_t view_angles(g_interfaces->m_csgo_input->get_view_angle());
        vec3_t fwd, right, up(0.f, 0.f, 1.f);
        g_math->angle_vectors(view_angles, &fwd, &right, nullptr);
        fwd.z = 0.f;
        fwd.normalize();
        right = up.cross(fwd);
        float front = vecDelta.dot(fwd);
        float side = vecDelta.dot(right);
        vecOutOffScreenPos.x = flRadiusX * -side;
        vecOutOffScreenPos.y = flRadiusY * -front;
        flOutRot = RAD2DEG(std::atan2(vecOutOffScreenPos.x, vecOutOffScreenPos.y) + A_PI);
        float yaw_rad = DEG2RAD(-flOutRot);
        float sa = std::sin(yaw_rad);
        float ca = std::cos(yaw_rad);
        vecOutOffScreenPos.x = (ImGui::GetIO().DisplaySize.x / 2.f) + (flRadiusX * sa);
        vecOutOffScreenPos.y = (ImGui::GetIO().DisplaySize.y / 2.f) - (flRadiusY * ca);
        };

    vecTargetPos = pPawn->m_pGameSceneNode()->m_vecAbsOrigin();
    bIsOnScreen = g_math->world_to_screen(vecTargetPos, vecScreenPos);
    flLeeWayX = ImGui::GetIO().DisplaySize.x / 18.f;
    flLeeWayY = ImGui::GetIO().DisplaySize.y / 18.f;

    if (!bIsOnScreen ||
        vecScreenPos.x < -flLeeWayX ||
        vecScreenPos.x >(ImGui::GetIO().DisplaySize.x + flLeeWayX) ||
        vecScreenPos.y < -flLeeWayY ||
        vecScreenPos.y >(ImGui::GetIO().DisplaySize.y + flLeeWayY)) {
        vecViewOrigin = g_ctx->m_local_pawn->m_pGameSceneNode()->m_vecAbsOrigin();
        vecDelta = (vecTargetPos - vecViewOrigin).normalized();
        float dist = (vecTargetPos - vecViewOrigin).length();
        float triangleSize = baseSize * std::clamp(600.f / dist, 0.8f, 1.2f);
        flRadius = screenDistance * (ImGui::GetIO().DisplaySize.y / 480.f);

        float flRadiusX = screenDistance * 100.0f / 100.f;
        float flRadiusY = screenDistance * 100.0f / 100.f;

        GetOffScreenData(vecDelta, flRadiusX, flRadiusY, vecOffScreenPos, flOffScreenRotation);
        flOffScreenRotation = -flOffScreenRotation;

        vArrowScreenPos = ImVec2(vecOffScreenPos.x, vecOffScreenPos.y);
        float flAngle = flOffScreenRotation + 180.0f;

        vP1 = RotatePoint({ vArrowScreenPos.x, vArrowScreenPos.y + 18.f }, vArrowScreenPos, flAngle);
        vP2 = RotatePoint({ vArrowScreenPos.x - 4.f, vArrowScreenPos.y + 6.f }, vArrowScreenPos, flAngle);
        vP3 = RotatePoint({ vArrowScreenPos.x + 4.f, vArrowScreenPos.y + 6.f }, vArrowScreenPos, flAngle);
        vBackCenter = RotatePoint({ vArrowScreenPos.x, vArrowScreenPos.y + 8.f }, vArrowScreenPos, flAngle);

        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        ImU32 color = ImGui::ColorConvertFloat4ToU32(triangleColor.Value);

        hellcolor glow_color = triangleColor;
        glow_color.Value.w *= 0.6f;
        ImU32 glow_color_u32 = ImGui::ColorConvertFloat4ToU32(glow_color.Value);

        ImVec2 glow_points[3] = { vP1, vP2, vP3 };
        draw_list->AddShadowConvexPoly(glow_points, 3, glow_color_u32, 15.0f, ImVec2(0, 0));
        draw_list->AddShadowConvexPoly(glow_points, 3, glow_color_u32, 10.0f, ImVec2(0, 0));
        draw_list->AddShadowConvexPoly(glow_points, 3, glow_color_u32, 6.0f, ImVec2(0, 0));

        draw_list->Flags |= ImDrawListFlags_AntiAliasedFill;
        draw_list->PathClear();
        draw_list->PathLineTo(ImVec2(vP1.x, vP1.y));
        draw_list->PathLineTo(ImVec2(vP2.x, vP2.y));
        draw_list->PathLineTo(vBackCenter);
        draw_list->PathLineTo(ImVec2(vP3.x, vP3.y));
        draw_list->PathFillConvex(color);
    }
}

void c_overlay::hitmarker() {
    if (!GET_VAR(bool, MISC_PATH(m_bHitMarker2d)))
        return;

    const float current_time = static_cast<float>(ImGui::GetTime());
    auto* draw_list = ImGui::GetBackgroundDrawList();

    const float fade_duration = 0.6f;
    const float max_size = 8.0f;
    const float base_thickness = 1.5f;
    const float gap = 3.0f;
    const hellcolor base_color_var = GET_VAR(hellcolor, MISC_PATH(m_colHitMarker2d));

    if (!m_hit_markers2.empty()) {
        auto it = std::remove_if(m_hit_markers2.begin(), m_hit_markers2.end(),
            [current_time, fade_duration](const hit_marker_tt& marker) {
                return (current_time - static_cast<float>(marker.time)) > fade_duration;
            });
        if (it != m_hit_markers2.end()) {
            m_hit_markers2.erase(it, m_hit_markers2.end());
        }
    }

    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

    auto draw_gradient_line_2d = [&](const ImVec2& start, const ImVec2& end, const hellcolor& color_start, const hellcolor& color_end, float thickness, float anim_progress, int corner_type) {
        ImVec2 anim_start = start;
        ImVec2 anim_end = end;

        if (anim_progress < 1.0f) {
            float anim_offset = (max_size + gap) * 2.0f;
            ImVec2 corner_offset;
            if (corner_type == 0) {
                corner_offset = ImVec2(-anim_offset, -anim_offset);
            }
            else if (corner_type == 1) {
                corner_offset = ImVec2(-anim_offset, anim_offset);
            }
            else if (corner_type == 2) {
                corner_offset = ImVec2(anim_offset, -anim_offset);
            }
            else {
                corner_offset = ImVec2(anim_offset, anim_offset);
            }

            float ease = 1.0f - (1.0f - anim_progress) * (1.0f - anim_progress) * (1.0f - anim_progress);
            anim_start = ImVec2(
                start.x + corner_offset.x * (1.0f - ease),
                start.y + corner_offset.y * (1.0f - ease)
            );
            anim_end = ImVec2(
                end.x + corner_offset.x * (1.0f - ease),
                end.y + corner_offset.y * (1.0f - ease)
            );
        }

        const int segments = 8;
        for (int i = 0; i < segments; ++i) {
            float t1 = (float)i / (float)segments;
            float t2 = (float)(i + 1) / (float)segments;

            ImVec2 p1 = ImVec2(anim_start.x + (anim_end.x - anim_start.x) * t1, anim_start.y + (anim_end.y - anim_start.y) * t1);
            ImVec2 p2 = ImVec2(anim_start.x + (anim_end.x - anim_start.x) * t2, anim_start.y + (anim_end.y - anim_start.y) * t2);

            hellcolor segment_color = hellcolor(
                color_start.Value.x + (color_end.Value.x - color_start.Value.x) * t1,
                color_start.Value.y + (color_end.Value.y - color_start.Value.y) * t1,
                color_start.Value.z + (color_end.Value.z - color_start.Value.z) * t1,
                color_start.Value.w + (color_end.Value.w - color_start.Value.w) * t1
            );

            draw_list->AddLine(p1, p2, segment_color, thickness);
        }
        };

    for (auto& marker : m_hit_markers2) {
        float elapsed = current_time - static_cast<float>(marker.time);
        float alpha = 1.0f - (elapsed / fade_duration);
        const float anim_duration = 0.15f;
        const float anim_progress = std::min(1.0f, elapsed / anim_duration);

        hellcolor base_color = base_color_var;
        base_color.Value.w = alpha;

        hellcolor core = base_color;
        hellcolor fade_color = core;
        fade_color.Value.w = 0.0f;

        hellcolor glow_color = core;
        glow_color.Value.w = alpha * 0.3f;
        hellcolor glow_fade = glow_color;
        glow_fade.Value.w = 0.0f;

        float glow_radius = (max_size + gap) * 1.5f;

        const int glow_segments = 64;
        const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;
        const unsigned int vtx_base = draw_list->_VtxCurrentIdx;

        draw_list->PrimReserve(glow_segments * 3, glow_segments + 1);
        draw_list->PrimWriteVtx(center, uv, glow_color);

        for (int j = 0; j < glow_segments; ++j) {
            float angle = (float)j / (float)glow_segments * 2.0f * IM_PI;
            ImVec2 pos = ImVec2(center.x + cosf(angle) * glow_radius, center.y + sinf(angle) * glow_radius);
            draw_list->PrimWriteVtx(pos, uv, glow_fade);
        }

        for (int j = 0; j < glow_segments; ++j) {
            draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base));
            draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + j));
            draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + ((j + 1) % glow_segments)));
        }

        draw_gradient_line_2d(
            ImVec2(center.x - gap - max_size, center.y - gap - max_size),
            ImVec2(center.x - gap, center.y - gap),
            fade_color, core, base_thickness, anim_progress, 0
        );

        draw_gradient_line_2d(
            ImVec2(center.x - gap - max_size, center.y + gap + max_size),
            ImVec2(center.x - gap, center.y + gap),
            fade_color, core, base_thickness, anim_progress, 1
        );

        draw_gradient_line_2d(
            ImVec2(center.x + gap + max_size, center.y - gap - max_size),
            ImVec2(center.x + gap, center.y - gap),
            fade_color, core, base_thickness, anim_progress, 2
        );

        draw_gradient_line_2d(
            ImVec2(center.x + gap + max_size, center.y + gap + max_size),
            ImVec2(center.x + gap, center.y + gap),
            fade_color, core, base_thickness, anim_progress, 3
        );
    }
}

void c_overlay::add_notification(std::string str_text, bool is_miss, bool is_config) {
    if (str_text.find("Shot was not registered by server") != std::string::npos ||
        str_text.find("target already dead") != std::string::npos) {
        return;
    }

    notification_t notification;
    notification.m_str_text = str_text;

    if (is_config) {
        notification.m_fl_remove_time = ImGui::GetTime() + 2.f;
        notification.m_fl_alpha = 0.f;
        notification.m_b_is_miss = is_miss;
        notification.m_b_is_config = is_config;
        notification.m_fl_spawn_time = ImGui::GetTime();
        notification.m_fl_anim_offset = 0.f;
        notification.m_fl_target_offset = 0.f;
        notification.m_fl_bounce_offset = 0.f;
        notification.m_fl_scale = 0.3f;
        notification.m_b_appearing = true;
        notification.m_b_disappearing = false;

        while (m_config_notifications.size() > 5)
            m_config_notifications.pop_back();

        m_config_notifications.emplace_front(notification);
    }
    else {
        if (!g_interfaces->m_global_vars)
            return;
        notification.m_fl_remove_time = g_interfaces->m_global_vars->m_curtime + 5.f;
        notification.m_fl_alpha = 0.f;
        notification.m_b_is_miss = is_miss;
        notification.m_b_is_config = is_config;
        notification.m_fl_spawn_time = g_interfaces->m_global_vars->m_curtime;
        notification.m_fl_anim_offset = 0.f;
        notification.m_fl_target_offset = 0.f;
        notification.m_fl_bounce_offset = 0.f;
        notification.m_fl_scale = 0.3f;
        notification.m_b_appearing = true;
        notification.m_b_disappearing = false;

        while (m_queue_notifications.size() > 15)
            m_queue_notifications.pop_back();

        m_queue_notifications.emplace_front(notification);
    }
}

void c_overlay::draw_notifications() {
    ImDrawList* p_draw_list = ImGui::GetBackgroundDrawList();
    hellcolor accent_color = hellcolor(140, 90, 190, 255);

    for (auto it = m_config_notifications.begin(); it != m_config_notifications.end(); ) {
        float current_time = ImGui::GetTime();
        float time_alive = current_time - it->m_fl_spawn_time;

        if (current_time > it->m_fl_remove_time && !it->m_b_disappearing) {
            it->m_b_disappearing = true;
            it->m_b_appearing = false;
        }

        if (it->m_b_appearing) {
            float appear_duration = 0.3f;
            float t = std::min(time_alive / appear_duration, 1.0f);

            float ease_out = 1.0f - powf(1.0f - t, 3.0f);
            it->m_fl_scale = 0.3f + (0.7f * ease_out);
            it->m_fl_alpha = t;
            it->m_fl_anim_offset = 0.f;

            if (t >= 1.0f) {
                it->m_b_appearing = false;
                it->m_fl_scale = 1.0f;
                it->m_fl_anim_offset = 0.f;
            }
        }
        else if (it->m_b_disappearing) {
            float disappear_duration = 0.3f;
            float disappear_start = current_time - it->m_fl_remove_time;
            float t = std::min(disappear_start / disappear_duration, 1.0f);

            it->m_fl_scale = 1.0f - (t * 0.3f);
            it->m_fl_alpha = 1.0f - t;
            it->m_fl_anim_offset = 0.f;
        }

        if (it->m_fl_alpha < 0.05f && it->m_b_disappearing) {
            it = m_config_notifications.erase(it);
        }
        else {
            ++it;
        }
    }

    if (!m_config_notifications.empty()) {
        ImFont* font = g_font_manager->m_gheist_medium_14;
        if (!font)
            font = g_font_manager->m_verdana_12;

        ImVec2 screen_size = ImGui::GetIO().DisplaySize;
        float padding_x = 8.0f;
        float padding_y = 6.0f;
        float rounding = 4.0f;
        float fl_start_y = screen_size.y - 10.0f;

        for (auto it = m_config_notifications.rbegin(); it != m_config_notifications.rend(); ++it) {
            auto& notification = *it;

            ImVec2 v_text_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, notification.m_str_text.c_str());
            float rect_height = v_text_size.y + padding_y * 2.0f;
            float rect_width = padding_x + v_text_size.x + padding_x;
            float fl_start_x = screen_size.x - rect_width - 10.0f + notification.m_fl_anim_offset + notification.m_fl_bounce_offset;

            fl_start_y -= rect_height + 3.0f;

            ImVec2 gradient_min = ImVec2(fl_start_x, fl_start_y + 2.f);
            ImVec2 gradient_max = ImVec2(fl_start_x + rect_width, fl_start_y + 3.f + 5.f - 2.f);

            hellcolor gradient_accent_color = accent_color;
            gradient_accent_color.Value.w = notification.m_fl_alpha;

            ImVec2 rect_min = ImVec2(fl_start_x, fl_start_y + 3.0f);
            ImVec2 rect_max = ImVec2(fl_start_x + rect_width, fl_start_y + 3.0f + rect_height);

            hellcolor bg_color(29, 29, 29, static_cast<int>(255 * notification.m_fl_alpha));

            float x_pos = fl_start_x + padding_x;
            float y_pos = fl_start_y + 3.0f + padding_y;

            hellcolor base_text_color(255, 255, 255, static_cast<int>(255 * notification.m_fl_alpha));

            std::string text = notification.m_str_text;

            auto draw_text_with_shadow = [&](const char* text_part, hellcolor color) {
                if (!text_part)
                    return;

                ImVec2 part_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, text_part);
                p_draw_list->AddText(font, font->LegacySize, ImVec2(x_pos, y_pos), color, text_part);
                x_pos += part_size.x;
                };

            draw_text_with_shadow(text.c_str(), base_text_color);

            fl_start_y -= 2.0f;
        }
    }

    std::vector<bool> event_list = GET_VAR(std::vector<bool>, MISC_PATH(m_hitlog_modes));
    if (event_list.empty())
        return;

    const bool is_in_game = g_interfaces->m_engine && g_interfaces->m_engine->is_connected() && g_interfaces->m_engine->in_game();
    const bool is_menu_open = g_menu && g_menu->m_menu_open;

    bool has_active_notifications = false;
    if (is_in_game && !m_queue_notifications.empty()) {
        for (auto it = m_queue_notifications.begin(); it != m_queue_notifications.end(); ) {
            bool should_show = false;
            if (it->m_b_is_miss) {
                should_show = (event_list.size() > 1) ? event_list[1] : false;
            }
            else {
                should_show = (event_list.size() > 0) ? event_list[0] : false;
            }

            if (should_show) {
                has_active_notifications = true;
                break;
            }
            ++it;
        }
    }

    const bool show_preview = is_menu_open && !has_active_notifications;

    if (!is_in_game && !show_preview)
        return;

    if (is_in_game && !has_active_notifications && !show_preview)
        return;

    if (!g_interfaces->m_global_vars)
        return;

    float fl_start_y = 10.f;

    if (show_preview && !has_active_notifications) {
        notification_t preview_hit;
        notification_t preview_miss;
        bool has_hit = false;
        bool has_miss = false;

        if (event_list.size() > 0 && event_list[0]) {
            preview_hit.m_str_text = "Hit PlayerName in the Head for 98 damage";
            preview_hit.m_fl_remove_time = 0.0f;
            preview_hit.m_fl_alpha = 1.0f;
            preview_hit.m_b_is_miss = false;
            has_hit = true;
        }

        if (event_list.size() > 1 && event_list[1]) {
            preview_miss.m_str_text = "Missed PlayerName in the Head due to spread";
            preview_miss.m_fl_remove_time = 0.0f;
            preview_miss.m_fl_alpha = 1.0f;
            preview_miss.m_b_is_miss = true;
            has_miss = true;
        }

        if (has_hit || has_miss) {
            std::vector<notification_t> preview_notifications;
            if (has_hit) preview_notifications.push_back(preview_hit);
            if (has_miss) preview_notifications.push_back(preview_miss);

            for (const auto& preview : preview_notifications) {
                ImFont* font = g_font_manager->m_gheist_medium_14;
                if (!font)
                    font = g_font_manager->m_verdana_12;

                ImVec2 v_text_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, preview.m_str_text.c_str());

                float padding_x = 8.0f;
                float padding_y = 6.0f;
                float rect_height = v_text_size.y + padding_y * 2.0f;
                float rect_width = padding_x + v_text_size.x + padding_x;
                float rounding = 4.0f;

                hellcolor accent_rect_color = accent_color;

                ImVec2 gradient_min = ImVec2(10, fl_start_y + 2.f);
                ImVec2 gradient_max = ImVec2(10 + rect_width, fl_start_y + 3.f + 5.f - 2.f);


                ImVec2 rect_min = ImVec2(10, fl_start_y + 3.0f);
                ImVec2 rect_max = ImVec2(10 + rect_width, fl_start_y + 3.0f + rect_height);

                hellcolor bg_color(29, 29, 29, 255);

                float x_pos = 10.0f + padding_x;
                float y_pos = fl_start_y + 3.0f + padding_y;

                hellcolor base_text_color(255, 255, 255, 255);

                std::string text = preview.m_str_text;

                auto draw_text_with_shadow = [&](const char* text_part, hellcolor color) {
                    if (!text_part)
                        return;

                    ImVec2 part_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, text_part);
                    p_draw_list->AddText(font, font->LegacySize, ImVec2(x_pos, y_pos), color, text_part);
                    x_pos += part_size.x;
                    };

                draw_text_with_shadow(text.c_str(), base_text_color);

                fl_start_y += rect_height + 3.0f + 2.0f;
            }
        }
        return;
    }

    if (!is_in_game) {
        return;
    }

    if (m_queue_notifications.empty()) {
        return;
    }

    for (auto it = m_queue_notifications.begin(); it != m_queue_notifications.end(); ) {
        bool should_show = false;
        if (it->m_b_is_miss) {
            should_show = (event_list.size() > 1) ? event_list[1] : false;
        }
        else {
            should_show = (event_list.size() > 0) ? event_list[0] : false;
        }

        if (!should_show) {
            ++it;
            continue;
        }

        if (!g_interfaces->m_global_vars) {
            ++it;
            continue;
        }

        float current_time = g_interfaces->m_global_vars->m_curtime;
        float time_alive = current_time - it->m_fl_spawn_time;

        if (current_time > it->m_fl_remove_time && !it->m_b_disappearing) {
            it->m_b_disappearing = true;
            it->m_b_appearing = false;
        }

        if (it->m_b_appearing) {
            float appear_duration = 0.3f;
            float t = std::min(time_alive / appear_duration, 1.0f);

            float ease_out = 1.0f - powf(1.0f - t, 3.0f);
            it->m_fl_scale = 0.3f + (0.7f * ease_out);
            it->m_fl_alpha = t;
            it->m_fl_anim_offset = 0.f;

            if (t >= 1.0f) {
                it->m_b_appearing = false;
                it->m_fl_scale = 1.0f;
                it->m_fl_anim_offset = 0.f;
            }
        }
        else if (it->m_b_disappearing) {
            float disappear_duration = 0.3f;
            float disappear_start = current_time - it->m_fl_remove_time;
            float t = std::min(disappear_start / disappear_duration, 1.0f);

            it->m_fl_scale = 1.0f - (t * 0.3f);
            it->m_fl_alpha = 1.0f - t;
            it->m_fl_anim_offset = 0.f;
        }

        if (it->m_fl_alpha < 0.05f && it->m_b_disappearing) {
            it = m_queue_notifications.erase(it);
        }
        else {
            ImFont* font = g_font_manager->m_gheist_medium_14;
            if (!font)
                font = g_font_manager->m_verdana_12;

            ImVec2 v_text_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, it->m_str_text.c_str());

            float padding_x = 8.0f * it->m_fl_scale;
            float padding_y = 6.0f * it->m_fl_scale;
            float rect_height = (v_text_size.y + padding_y * 2.0f) * it->m_fl_scale;
            float rect_width = (padding_x + v_text_size.x + padding_x) * it->m_fl_scale;
            float rounding = 4.0f * it->m_fl_scale;

            float scaled_font_size = font->LegacySize * it->m_fl_scale;

            hellcolor accent_rect_color = accent_color;
            accent_rect_color.Value.w = it->m_fl_alpha;

            float scale_offset = (1.0f - it->m_fl_scale) * rect_width * 0.5f;

            ImVec2 gradient_min = ImVec2(10 + it->m_fl_anim_offset + scale_offset, fl_start_y + 2.f);
            ImVec2 gradient_max = ImVec2(10 + rect_width + it->m_fl_anim_offset + scale_offset, fl_start_y + 3.f + 5.f - 2.f);


            ImVec2 rect_min = ImVec2(10 + it->m_fl_anim_offset + scale_offset, fl_start_y + 3.0f * it->m_fl_scale);
            ImVec2 rect_max = ImVec2(10 + rect_width + it->m_fl_anim_offset + scale_offset, fl_start_y + 3.0f * it->m_fl_scale + rect_height);

            hellcolor bg_color(29, 29, 29, static_cast<int>(255 * it->m_fl_alpha));


            float x_pos = 10.0f + padding_x + it->m_fl_anim_offset + scale_offset;
            float y_pos = fl_start_y + 3.0f * it->m_fl_scale + padding_y;

            hellcolor base_text_color(255, 255, 255, static_cast<int>(255 * it->m_fl_alpha));

            std::string text = it->m_str_text;

            auto draw_text_with_shadow = [&](const char* text_part, hellcolor color) {
                if (!text_part)
                    return;

                ImVec2 part_size = font->CalcTextSizeA(scaled_font_size, FLT_MAX, 0.0f, text_part);
                p_draw_list->AddText(font, scaled_font_size, ImVec2(x_pos, y_pos), color, text_part);
                x_pos += part_size.x;
                };

            draw_text_with_shadow(text.c_str(), base_text_color);

            fl_start_y += rect_height + 3.0f * it->m_fl_scale + 2.0f;
            ++it;
        }
    }
}
void c_overlay::draw_ragebot_indicators() {
    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;
    if (!g_ctx->m_active_weapon || !g_ctx->m_local_controller)
        return;

    int weapon_type = get_ragebot_weapon_type(g_ctx->m_weapon_def_index);
    if (weapon_type == 10) return;

    auto draw_list = ImGui::GetBackgroundDrawList();
    ImFont* font = g_font_manager->m_gheist_medium_14 ? g_font_manager->m_gheist_medium_14 : g_font_manager->m_inter_4002;
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;

    struct IndicatorInfo {
        std::string label;
        std::string value;
    };
    std::vector<IndicatorInfo> indicators;
    bool nospread = GET_VAR(bool, RAGEBOT_PATH(m_nospread));
  
    bool force_shoot = GET_VAR(bool, RAGEBOT_PATH(m_force_shoot));
    bool safenospread = GET_VAR(bool, RAGEBOT_PATH(m_safenospread));
    int min_damage = GET_VAR(int, tabs::aimbot::get_min_damage_holder_id(weapon_type));
    int hitchance = GET_VAR(int, tabs::aimbot::get_hitchance_holder_id(weapon_type));
    int pointscale = GET_VAR(int, tabs::aimbot::get_pointscale_holder_id(weapon_type));

    if (nospread)
        indicators.push_back({ "NS", safenospread ? "safe" : "on" });
    if (min_damage > 0)
        indicators.push_back({ "MD", std::to_string(min_damage) });
    if (hitchance > 0)
        indicators.push_back({ "HC", std::to_string(hitchance) });
    if (pointscale > 0)
        indicators.push_back({ "PS", std::to_string(pointscale) });
    if (force_shoot)
        indicators.push_back({ "FS", "on" });

    if (indicators.empty()) return;

    const float padding_x = 10.f;
    const float item_height = 22.f;
    const float gap = 4.f;   
    const float base_width = 100.f;
    const float rounding = 4.f;


    float max_width = base_width;
    for (const auto& ind : indicators) {
        ImVec2 ls = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, ind.label.c_str());
        ImVec2 vs = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, ind.value.c_str());
        max_width = std::max(max_width, ls.x + vs.x + padding_x * 3.f);
    }

   
    ImVec2 base_pos = ImVec2(screen_size.x - max_width - 20.f, screen_size.y * 0.35f);

    hellcolor accent(140, 90, 190, 255);

    for (int i = 0; i < (int)indicators.size(); i++) {
        const auto& ind = indicators[i];

 
        ImVec2 panel_min = ImVec2(base_pos.x, base_pos.y + i * (item_height + gap));
        ImVec2 panel_max = ImVec2(base_pos.x + max_width, panel_min.y + item_height);

    
        if (hell::blur::m_initialized) {
            ImVec2 rect_size = ImVec2(panel_max.x - panel_min.x, panel_max.y - panel_min.y);
            hell::blur::blur_effect.begin_blur();
            hell::blur::blur_effect.apply_blur(draw_list, panel_min, rect_size, rounding, 6.0f, ImDrawFlags_RoundCornersAll);
            hell::blur::blur_effect.end_blur();
        }


        draw_list->AddRectFilled(panel_min, panel_max, hellcolor(20, 20, 22, 160), rounding);


        ImVec2 ls = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, ind.label.c_str());
        ImVec2 vs = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, ind.value.c_str());

        ImVec2 label_pos = ImVec2(panel_min.x + padding_x,
            panel_min.y + (item_height - ls.y) * 0.5f);
        ImVec2 value_pos = ImVec2(panel_max.x - padding_x - vs.x,
            panel_min.y + (item_height - vs.y) * 0.5f);

        draw_list->AddText(font, font->LegacySize, label_pos, hellcolor(255, 255, 255, 220), ind.label.c_str());
        draw_list->AddText(font, font->LegacySize, value_pos, accent, ind.value.c_str());
    }
}
void c_overlay::hotkey_list() {
    if (!GET_VAR(bool, VISUALS_PATH(m_enabled_hotkey_list)))
        return;

  
    const bool is_menu_open = g_menu && g_menu->m_menu_open;

    struct KeybindInfo {
        std::string display_text;
        std::string status_text;
        fnv1a_t holder_id;
        float activation_time;
    };

    static std::vector<KeybindInfo> persistent_keybinds;
    std::vector<KeybindInfo> current_active;

    auto& widget_keybinds = GET_VAR(kb_map_t, CONFIG_PATH(m_widget_keybinds));
    for (const auto& [widget_id, binds] : widget_keybinds) {
        for (const auto& keybind : binds) {
            if (keybind.m_key == -1) continue;
            std::string display_text = get_keybind_name(keybind.m_holder_id);
            if (display_text.empty()) continue;

            bool is_mindamage_keybind = (
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_light_pistol ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_deagle ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_revolver ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_smg ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_lmg ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_ar ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_shotgun ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_scout ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_autosniper ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_awp
                );

            bool is_hitchance_keybind = (
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_light_pistol ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_deagle ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_revolver ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_smg ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_lmg ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_ar ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_shotgun ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_scout ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_autosniper ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_awp
                );

            bool is_pointscale_keybind = (
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_light_pistol ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_deagle ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_revolver ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_smg ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_lmg ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_ar ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_shotgun ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_scout ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_autosniper ||
                keybind.m_holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_awp
                );

            if (is_mindamage_keybind || is_hitchance_keybind || is_pointscale_keybind) {
                if (!g_ctx->m_active_weapon) continue;
                int weapon_rage_type = get_ragebot_weapon_type(g_ctx->m_weapon_def_index);
                if (weapon_rage_type == 10) continue;

                fnv1a_t current_weapon_holder_id;
                if (is_mindamage_keybind)
                    current_weapon_holder_id = tabs::aimbot::get_min_damage_holder_id(weapon_rage_type);
                else if (is_hitchance_keybind)
                    current_weapon_holder_id = tabs::aimbot::get_hitchance_holder_id(weapon_rage_type);
                else if (is_pointscale_keybind)
                    current_weapon_holder_id = tabs::aimbot::get_pointscale_holder_id(weapon_rage_type);

                if (keybind.m_holder_id != current_weapon_holder_id) continue;
            }

            bool key_pressed = g_input->key_down(keybind.m_key);
            bool should_show = false;
            std::string status_text;

            if (keybind.m_keybind_type == e_keybind_type::keybind_type_checkbox) {
                bool current_value = GET_VAR(bool, keybind.m_holder_id);
                if (keybind.m_mode == e_key_mode::key_mode_hold) {
                    if (key_pressed) {
                        should_show = true;
                        status_text = keybind.m_is_on_mode ? "ON" : "OFF";
                    }
                }
                else if (keybind.m_mode == e_key_mode::key_mode_toggle) {
                    if (keybind.m_is_on_mode) {
                        if (keybind.m_on_mode_activated && current_value) {
                            should_show = true;
                            status_text = "ON";
                        }
                    }
                    else {
                        if (keybind.m_keybind_active && !current_value) {
                            should_show = true;
                            status_text = "OFF";
                        }
                    }
                }
            }
            else if (keybind.m_keybind_type == e_keybind_type::keybind_type_slider_int) {
                if (keybind.m_keybind_active && std::holds_alternative<int>(keybind.m_override_val)) {
                    should_show = true;
                    int override_val = std::get<int>(keybind.m_override_val);
                    status_text = std::to_string(override_val);
                }
            }
            else if (keybind.m_keybind_type == e_keybind_type::keybind_type_slider_float) {
                if (keybind.m_keybind_active && std::holds_alternative<float>(keybind.m_override_val)) {
                    should_show = true;
                    float override_val = std::get<float>(keybind.m_override_val);
                    status_text = std::to_string((int)override_val);
                }
            }

            if (should_show) {
                auto existing = std::find_if(persistent_keybinds.begin(), persistent_keybinds.end(),
                    [&](const KeybindInfo& info) { return info.holder_id == keybind.m_holder_id; });

                if (existing != persistent_keybinds.end()) {
                    existing->display_text = display_text;
                    existing->status_text = status_text;
                    current_active.push_back(*existing);
                }
                else {
                    KeybindInfo new_bind = { display_text, status_text, keybind.m_holder_id, static_cast<float>(ImGui::GetTime()) };
                    persistent_keybinds.push_back(new_bind);
                    current_active.push_back(new_bind);

                    m_hotkey_anim.keybind_activation_times[keybind.m_holder_id] = ImGui::GetTime();
                    m_hotkey_anim.target_text_alphas[keybind.m_holder_id] = 1.0f;
                }
            }
        }
    }

    persistent_keybinds.erase(std::remove_if(persistent_keybinds.begin(), persistent_keybinds.end(),
        [&](const KeybindInfo& info) {
            bool found = std::find_if(current_active.begin(), current_active.end(),
                [&](const KeybindInfo& active) { return active.holder_id == info.holder_id; }) != current_active.end();

            if (!found)
                m_hotkey_anim.target_text_alphas[info.holder_id] = 0.0f;
            return !found;
        }), persistent_keybinds.end());

    std::sort(current_active.begin(), current_active.end(),
        [](const KeybindInfo& a, const KeybindInfo& b) {
            return a.activation_time < b.activation_time;
        });

    bool has_active = !current_active.empty();
    m_hotkey_anim.has_active_keybinds = has_active;

    bool should_show_widget = has_active || is_menu_open;

    static bool prev_menu_state = false;
    static bool prev_has_active = false;

    if (has_active && !prev_has_active)
        m_hotkey_anim.target_widget_alpha = 1.0f;
    else if (!has_active && prev_has_active)
        m_hotkey_anim.target_widget_alpha = 0.0f;

    float lerp_speed = 8.0f * ImGui::GetIO().DeltaTime;

    if (is_menu_open) {
        if (!has_active)
            m_hotkey_anim.widget_alpha = 1.0f;
        else
            m_hotkey_anim.widget_alpha += (m_hotkey_anim.target_widget_alpha - m_hotkey_anim.widget_alpha) * lerp_speed;
    }
    else {
        if (has_active)
            m_hotkey_anim.widget_alpha += (m_hotkey_anim.target_widget_alpha - m_hotkey_anim.widget_alpha) * lerp_speed;
        else {
            if (prev_menu_state && !is_menu_open)
                m_hotkey_anim.widget_alpha = 0.0f;
            else
                m_hotkey_anim.widget_alpha += (m_hotkey_anim.target_widget_alpha - m_hotkey_anim.widget_alpha) * lerp_speed;
        }
    }

    prev_menu_state = is_menu_open;
    prev_has_active = has_active;

    if (!should_show_widget && m_hotkey_anim.widget_alpha < 0.01f) return;

    auto draw_list = ImGui::GetBackgroundDrawList();
    ImFont* font = g_font_manager->m_gheist_medium_14 ? g_font_manager->m_gheist_medium_14 : g_font_manager->m_inter_4002;
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;


    const float padding_x = 10.f;
   
    const float bar_height = 3.f;      
    const float header_height = 26.f;   
    const float item_height = 26.f;      
    const float base_width = 114;
    const float base_content_height = 18.f;






    float max_width = base_width;
    for (const auto& bind : current_active) {
        ImVec2 name_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, bind.display_text.c_str());

        std::string display_status = bind.status_text;
        if (display_status == "ON") {
            display_status = "on";
        }
        ImVec2 status_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, display_status.c_str());

        float total_width = name_size.x + status_size.x + padding_x * 3.f;
        max_width = std::max(max_width, total_width);
    }

    m_hotkey_anim.target_widget_width = max_width;
    m_hotkey_anim.widget_width = max_width;

    float content_height = has_active ? (current_active.size() * item_height) : (base_content_height * 0.5f);
    float total_height = header_height + bar_height + content_height;
    m_hotkey_anim.target_base_rect_height = total_height;

    m_hotkey_anim.base_rect_height += (m_hotkey_anim.target_base_rect_height - m_hotkey_anim.base_rect_height) * lerp_speed;

    static bool position_initialized = false;
    if (!position_initialized || m_hotkey_anim.widget_pos.x < 0.0f || m_hotkey_anim.widget_pos.y < 0.0f) {
        m_hotkey_anim.widget_pos = ImVec2(screen_size.x - 200.f, screen_size.y * 0.35f);
        position_initialized = true;
    }

    if (is_menu_open) {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        ImVec2 rect_min = m_hotkey_anim.widget_pos;
        ImVec2 rect_max = ImVec2(rect_min.x + m_hotkey_anim.widget_width, rect_min.y + m_hotkey_anim.base_rect_height);

        bool hovered = mouse_pos.x >= rect_min.x && mouse_pos.x <= rect_max.x && mouse_pos.y >= rect_min.y && mouse_pos.y <= rect_max.y;

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_hotkey_anim.dragging = true;
            m_hotkey_anim.drag_offset = ImVec2(mouse_pos.x - rect_min.x, mouse_pos.y - rect_min.y);
        }

        if (m_hotkey_anim.dragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_hotkey_anim.widget_pos = ImVec2(mouse_pos.x - m_hotkey_anim.drag_offset.x,
                    mouse_pos.y - m_hotkey_anim.drag_offset.y);
            }
            else
                m_hotkey_anim.dragging = false;
        }
    }


    ImVec2 header_min = m_hotkey_anim.widget_pos;
    ImVec2 header_max = ImVec2(m_hotkey_anim.widget_pos.x + m_hotkey_anim.widget_width, m_hotkey_anim.widget_pos.y + header_height);

    hellcolor header_bg_color(28, 28, 30, 255);
    draw_list->AddRectFilled(header_min, header_max, header_bg_color, 0, ImDrawFlags_RoundCornersTop);

    ImVec2 header_text_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, "Hot Keys List");
    ImVec2 header_text_pos = ImVec2(header_min.x + (m_hotkey_anim.widget_width - header_text_size.x) * 0.5f, header_min.y + (header_height - header_text_size.y) * 0.5f);
    hellcolor header_text_color(255, 255, 255,255);
    draw_list->AddText(font, font->LegacySize, header_text_pos, header_text_color, "Hot Keys List");

    hellcolor grad_edge(20, 25, 50, 255);        
    hellcolor grad_purple(60, 45, 100, 255);     
    hellcolor grad_glow(120, 90, 180, 255);      
    ImVec2 bar_min = ImVec2(header_min.x, header_max.y);
    ImVec2 bar_max = ImVec2(header_max.x, header_max.y + bar_height);

    float bar_w = bar_max.x - bar_min.x;
    float x0 = bar_min.x;
    float x_35 = bar_min.x + bar_w * 0.35f;
    float x_50 = bar_min.x + bar_w * 0.50f;
    float x_65 = bar_min.x + bar_w * 0.65f;
    float x_100 = bar_max.x;

    // edge -> purple (0% .. 35%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x0, bar_min.y), ImVec2(x_35, bar_max.y),
        grad_edge, grad_purple, grad_purple, grad_edge
    );
    // purple -> glow (35% .. 50%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_35, bar_min.y), ImVec2(x_50, bar_max.y),
        grad_purple, grad_glow, grad_glow, grad_purple
    );
    // glow -> purple (50% .. 65%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_50, bar_min.y), ImVec2(x_65, bar_max.y),
        grad_glow, grad_purple, grad_purple, grad_glow
    );
    // purple -> edge (65% .. 100%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_65, bar_min.y), ImVec2(x_100, bar_max.y),
        grad_purple, grad_edge, grad_edge, grad_purple
    );


    ImVec2 content_min = ImVec2(m_hotkey_anim.widget_pos.x, bar_max.y);
    ImVec2 content_max = ImVec2(m_hotkey_anim.widget_pos.x + m_hotkey_anim.widget_width, m_hotkey_anim.widget_pos.y + m_hotkey_anim.base_rect_height);

    if (hell::blur::m_initialized) {
        ImVec2 rect_size = ImVec2(content_max.x - content_min.x, content_max.y - content_min.y);
        hell::blur::blur_effect.begin_blur();
        hell::blur::blur_effect.apply_blur(draw_list, content_min, rect_size, 0.0f, 6.0f, ImDrawFlags_RoundCornersBottom);
        hell::blur::blur_effect.end_blur();
    }

    hellcolor content_bg_color(20, 20, 22, (int)(150 * m_hotkey_anim.widget_alpha));
    draw_list->AddRectFilled(content_min, content_max, content_bg_color, 0, ImDrawFlags_RoundCornersBottom);

    if (has_active) {
        hellcolor accent_color = hellcolor(140, 90, 190, 255);

        draw_list->PushClipRect(content_min, content_max, true);
        float current_y = content_min.y;

        for (const auto& bind : current_active) {
            if (m_hotkey_anim.text_alphas.find(bind.holder_id) == m_hotkey_anim.text_alphas.end())
                m_hotkey_anim.text_alphas[bind.holder_id] = 0.0f;

            if (m_hotkey_anim.target_text_alphas.find(bind.holder_id) == m_hotkey_anim.target_text_alphas.end())
                m_hotkey_anim.target_text_alphas[bind.holder_id] = 1.0f;

            float& alpha = m_hotkey_anim.text_alphas[bind.holder_id];
            float target_alpha = m_hotkey_anim.target_text_alphas[bind.holder_id];
            alpha += (target_alpha - alpha) * lerp_speed;

            if (alpha > 0.01f) {
                ImVec2 name_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, bind.display_text.c_str());

                std::string display_status = bind.status_text;
                if (display_status == "ON") {
                    display_status = "on";
                }
                ImVec2 status_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, display_status.c_str());

                ImVec2 name_pos = ImVec2(content_min.x + padding_x, current_y + (item_height - name_size.y) * 0.5f);
                ImVec2 status_pos = ImVec2(content_max.x - padding_x - status_size.x, current_y + (item_height - status_size.y) * 0.5f);

                hellcolor name_color(255, 255, 255, (int)(255 * alpha * m_hotkey_anim.widget_alpha));

                hellcolor status_color = accent_color;
                status_color.Value.w *= alpha * m_hotkey_anim.widget_alpha;

                draw_list->AddText(font, font->LegacySize, name_pos, name_color, bind.display_text.c_str());
                draw_list->AddText(font, font->LegacySize, status_pos, status_color, display_status.c_str());
            }

            current_y += item_height;
        }
        draw_list->PopClipRect();
    }
}
std::string c_overlay::get_keybind_name(fnv1a_t holder_id) {
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_enabled_ragebot) return "Ragebot";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_autofire) return "Auto Fire";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_silent_aim) return "Silent Aim";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_autostop) return "Auto Stop";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_taser_bot) return "Taser Bot";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_auto_scope) return "Auto Scope";

    if (holder_id == g_vars->m_aimbot.m_antiaim.m_enabled_antiaim) return "Anti-Aim";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_at_target) return "At Target";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_hide_shots) return "Hide Shots";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_override_left) return "Override Left";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_override_right) return "Override Right";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_yaw_jitter) return "Yaw Jitter";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_pitch_jitter) return "Pitch Jitter";

    if (holder_id == g_vars->m_visuals.m_visualize_hitboxes) return "Visualize Hitboxes";
    if (holder_id == g_vars->m_visuals.m_shot_sparks_enabled) return "Shot Sparks";
    if (holder_id == g_vars->m_visuals.m_enabled_kill_effects) return "Kill Effects";
    if (holder_id == g_vars->m_visuals.m_chams_enabled) return "Chams";
    if (holder_id == g_vars->m_visuals.m_chams_enabled_bt) return "Backtrack Chams";
    if (holder_id == g_vars->m_visuals.m_chams_enabled_os) return "On Shot Chams";
    if (holder_id == g_vars->m_visuals.m_chams_local_enabled) return "Local Chams";
    if (holder_id == g_vars->m_visuals.m_chams_local_enabled_bt) return "Local BT Chams";
    if (holder_id == g_vars->m_visuals.m_esp_enabled) return "ESP";
    if (holder_id == g_vars->m_visuals.m_bbox) return "Bounding Box";
    if (holder_id == g_vars->m_visuals.m_visible_only) return "Visible Only";
    if (holder_id == g_vars->m_visuals.m_name) return "Name ESP";
    if (holder_id == g_vars->m_visuals.m_ping) return "Ping ESP";
    if (holder_id == g_vars->m_visuals.m_weapon_text) return "Weapon Text";
    if (holder_id == g_vars->m_visuals.m_weapon_icon) return "Weapon Icon";
    if (holder_id == g_vars->m_visuals.m_health_bar) return "Health Bar";
    if (holder_id == g_vars->m_visuals.m_armor_bar) return "Armor Bar";
    if (holder_id == g_vars->m_visuals.m_skeleton_esp_enabled) return "Skeleton ESP";
    if (holder_id == g_vars->m_visuals.m_third_person_enabled) return "Third Person";
    if (holder_id == g_vars->m_visuals.m_remove_visual_punch) return "No Visual Recoil";
    if (holder_id == g_vars->m_visuals.m_enable_world_modulation) return "World Modulation";

    if (holder_id == g_vars->m_visuals.m_enable_inferno_radius) return "Inferno Radius";

    if (holder_id == g_vars->m_visuals.m_local_glow) return "Local Glow";
    if (holder_id == g_vars->m_visuals.m_transparency_in_scope) return "Scope Transparency";
    if (holder_id == g_vars->m_visuals.m_teamate_glow) return "Teammate Glow";
    if (holder_id == g_vars->m_visuals.m_enemy_glow) return "Enemy Glow";
    if (holder_id == g_vars->m_visuals.m_scope_overlay) return "Scope Overlay";

    if (holder_id == g_vars->m_visuals.m_enabled_spread_circle) return "Spread Circle";
    if (holder_id == g_vars->m_visuals.m_bomb_hud_enabled) return "Bomb HUD";
    if (holder_id == g_vars->m_visuals.m_enabled_spread_gap) return "Spread Gap";
    if (holder_id == g_vars->m_visuals.m_enabled_hotkey_list) return "Hotkey List";


    if (holder_id == g_vars->m_misc.m_enabled_bunny_hop) return "Bunny Hop";

    if (holder_id == g_vars->m_misc.m_enabled_slow_walk) return "Slow Walk";
    if (holder_id == g_vars->m_misc.m_enabled_auto_peek) return "Auto Peek";
    if (holder_id == g_vars->m_misc.m_enabled_autostrafe) return "Auto Strafe";
    if (holder_id == g_vars->m_misc.m_enabled_hitsound) return "Hit Sound";
    if (holder_id == g_vars->m_misc.m_enabled_watermark) return "Watermark";



    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_light_pistol) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_deagle) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_revolver) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_smg) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_lmg) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_ar) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_shotgun) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_scout) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_autosniper) return "Min Damage Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_mindamage_awp) return "Min Damage Override";

    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_light_pistol) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_deagle) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_revolver) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_smg) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_lmg) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_ar) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_shotgun) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_scout) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_autosniper) return "Hit Chance Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_hitchance_awp) return "Hit Chance Override";

    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_light_pistol) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_deagle) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_revolver) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_smg) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_lmg) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_ar) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_shotgun) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_scout) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_autosniper) return "Point Scale Override";
    if (holder_id == g_vars->m_aimbot.m_ragebot.m_pointscale_awp) return "Point Scale Override";

    if (holder_id == g_vars->m_aimbot.m_antiaim.m_yaw) return "Yaw";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_pitch) return "Pitch";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_yaw_jitter_amount) return "Yaw Jitter Amount";
    if (holder_id == g_vars->m_aimbot.m_antiaim.m_pitch_jitter_amount) return "Pitch Jitter Amount";

    if (holder_id == g_vars->m_visuals.m_visualize_hitbox_fov) return "Hitbox FOV";
    if (holder_id == g_vars->m_visuals.m_kill_effects_type) return "Kill Effects Type";
    if (holder_id == g_vars->m_visuals.m_override_world_fov_value) return "World FOV";
    if (holder_id == g_vars->m_visuals.m_override_viewmodel_value) return "Viewmodel FOV";
    if (holder_id == g_vars->m_visuals.m_skeleton_esp_backtrack_enabled) return "Backtrack Skeleton";
    if (holder_id == g_vars->m_visuals.m_skeleton_esp_onshot_enabled) return "On Shot Skeleton";
    if (holder_id == g_vars->m_visuals.m_third_person_distance) return "Third Person Distance";
    if (holder_id == g_vars->m_visuals.m_scope_overlay_gap) return "Scope Gap";
    if (holder_id == g_vars->m_visuals.m_scope_overlay_length) return "Scope Length";

    if (holder_id == g_vars->m_misc.m_enabled_straight_throw) return "Straight Throw";
    if (holder_id == g_vars->m_misc.m_slow_walk_percent) return "Slow Walk %";

    if (holder_id == g_vars->m_misc.m_hitsound_volume) return "Hit Sound Volume";
    if (holder_id == g_vars->m_misc.m_hitsound_selection) return "Hit Sound Type";
    if (holder_id == g_vars->m_misc.m_grenade_release_damage) return "Grenade Release DMG %";
    if (holder_id == g_vars->m_misc.m_enabled_grenade_release) return "Grenade Release";

    return "Unknown";
}

void c_overlay::watermark() {
    if (!GET_VAR(bool, MISC_PATH(m_enabled_watermark)))
        return;
    auto draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    if (screen_size.x <= 0.f || screen_size.y <= 0.f)
        return;
    ImFont* font = g_font_manager->m_gheist_medium_14 ? g_font_manager->m_gheist_medium_14 : g_font_manager->m_inter_4002;
    if (!font)
        return;
    hellcolor accent_color = hellcolor(140, 90, 190, 255);
    static float smoothed_fps = 60.f;
    float raw_fps = std::clamp(ImGui::GetIO().Framerate, 1.f, 999.f);
    float smoothing_factor = std::clamp(ImGui::GetIO().DeltaTime * 5.f, 0.01f, 0.5f);
    smoothed_fps = std::clamp(smoothed_fps + (raw_fps - smoothed_fps) * smoothing_factor, 1.f, 999.f);
    int fps = static_cast<int>(smoothed_fps);
    int ping = 0;
    if (g_ctx->m_local_controller && g_interfaces->m_engine->is_connected() && g_interfaces->m_engine->in_game()) {
        ping = std::clamp(g_ctx->m_local_controller->m_iPing(), 0, 999);
    }
    char fps_buffer[8];
    char ping_buffer[8];
    std::snprintf(fps_buffer, sizeof(fps_buffer), "%d", fps);
    std::snprintf(ping_buffer, sizeof(ping_buffer), "%d", ping);
    const char* hellcore_text = "quint 2.0v";
    const char* fps_label = "Fps";
    const char* ms_label = "Ms";
    ImVec2 hellcore_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, hellcore_text);
    ImVec2 fps_label_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, fps_label);
    ImVec2 ms_label_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, ms_label);
    ImVec2 fps_num_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, fps_buffer);
    ImVec2 ping_num_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, ping_buffer);
    ImVec2 max_fps_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, "999");
    const float padding_x = 8.0f;    
    const float padding_y = 4.0f;      
    const float rounding = 0.0f;      
    const float spacing = 4.0f;
    const float bar_height = 2.0f;      

    float total_width = padding_x + hellcore_size.x + spacing + fps_label_size.x + spacing +
        max_fps_size.x + spacing + ms_label_size.x + spacing + ping_num_size.x + padding_x;
    float content_height = hellcore_size.y + padding_y * 2.0f;
    float total_height = bar_height + content_height;

    ImVec2 watermark_pos = ImVec2(screen_size.x - total_width - 10.0f, 10.0f);

    
    ImVec2 bar_min = watermark_pos;
    ImVec2 bar_max = ImVec2(watermark_pos.x + total_width, watermark_pos.y + bar_height);

    hellcolor grad_edge(20, 25, 50, 255);       
    hellcolor grad_purple(60, 45, 100, 255);     
    hellcolor grad_glow(120, 90, 180, 255);    
    float bar_w = bar_max.x - bar_min.x;
    float x0 = bar_min.x;
    float x_35 = bar_min.x + bar_w * 0.35f;
    float x_50 = bar_min.x + bar_w * 0.50f;
    float x_65 = bar_min.x + bar_w * 0.65f;
    float x_100 = bar_max.x;

    draw_list->PushClipRect(bar_min, bar_max, true);

    draw_list->AddRectFilledMultiColor(
        ImVec2(x0, bar_min.y), ImVec2(x_35, bar_max.y),
        grad_edge, grad_purple, grad_purple, grad_edge
    );
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_35, bar_min.y), ImVec2(x_50, bar_max.y),
        grad_purple, grad_glow, grad_glow, grad_purple
    );
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_50, bar_min.y), ImVec2(x_65, bar_max.y),
        grad_glow, grad_purple, grad_purple, grad_glow
    );
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_65, bar_min.y), ImVec2(x_100, bar_max.y),
        grad_purple, grad_edge, grad_edge, grad_purple
    );

    draw_list->PopClipRect();

   
    ImVec2 content_min = ImVec2(watermark_pos.x, bar_max.y);
    ImVec2 content_max = ImVec2(watermark_pos.x + total_width, watermark_pos.y + total_height);

    if (hell::blur::m_initialized) {
        ImVec2 rect_size = ImVec2(content_max.x - content_min.x, content_max.y - content_min.y);
        hell::blur::blur_effect.begin_blur();
      
        hell::blur::blur_effect.apply_blur(draw_list, content_min, rect_size, 0.0f, 0.0f, ImDrawFlags_RoundCornersNone);
        hell::blur::blur_effect.end_blur();
    }

    hellcolor content_bg_color(18, 18, 20, 110);
    draw_list->AddRectFilled(content_min, content_max, content_bg_color, 0.0f, ImDrawFlags_RoundCornersNone);

    hellcolor border_color(255, 255, 255, 22);
    draw_list->AddRect(content_min, content_max, border_color, 0.0f, ImDrawFlags_RoundCornersNone, 1.0f);


    float x_pos = watermark_pos.x + padding_x;
    float y_pos = content_min.y + (content_height - hellcore_size.y) * 0.5f;
    hellcolor white_color(255, 255, 255, 255);

    draw_list->AddText(font, font->LegacySize, ImVec2(x_pos, y_pos), accent_color, hellcore_text);
    x_pos += hellcore_size.x + spacing;

    draw_list->AddText(font, font->LegacySize, ImVec2(x_pos, y_pos), white_color, fps_label);
    x_pos += fps_label_size.x + spacing;

    float fps_center_x = x_pos + (max_fps_size.x - fps_num_size.x) * 0.5f;
    draw_list->AddText(font, font->LegacySize, ImVec2(fps_center_x, y_pos), accent_color, fps_buffer);
    x_pos += max_fps_size.x + spacing;

    draw_list->AddText(font, font->LegacySize, ImVec2(x_pos, y_pos), white_color, ms_label);
    x_pos += ms_label_size.x + spacing;

    draw_list->AddText(font, font->LegacySize, ImVec2(x_pos, y_pos), accent_color, ping_buffer);
}
void c_overlay::observer_list() {
    if (!GET_VAR(bool, VISUALS_PATH(m_enabled_observer_list)))
        return;

    const bool is_in_game = g_interfaces->m_engine->is_connected() && g_interfaces->m_engine->in_game();
    const bool is_menu_open = g_menu && g_menu->m_menu_open;

    struct observer_info {
        std::string player_name;
        uint64_t steam_id;
        float activation_time;
    };

    static std::vector<observer_info> persistent_observers;
    std::vector<observer_info> current_active;

    if (is_in_game && g_ctx->m_local_pawn) {
        bool local_is_spectator = false;
        c_cs_player_pawn* spectator_target = nullptr;

        auto local_observer_services = reinterpret_cast<player_observer_services*>(g_ctx->m_local_pawn->m_pObserverServices());
        if (local_observer_services && local_observer_services->m_iObserverMode() > 0) {
            local_is_spectator = true;
            spectator_target = local_observer_services->m_hObserverTarget().get<c_cs_player_pawn>();
        }

        for (auto& player : g_entity_cache->m_players) {
            if (!player.m_controller || player.m_controller == g_ctx->m_local_controller)
                continue;

            if (!player.m_pawn)
                continue;

            auto observer_services = reinterpret_cast<player_observer_services*>(player.m_pawn->m_pObserverServices());
            if (!observer_services)
                continue;

            if (observer_services->m_iObserverMode() <= 0)
                continue;

            auto observer_target = observer_services->m_hObserverTarget().get<c_cs_player_pawn>();
            if (!observer_target)
                continue;

            bool should_add = false;

            if (local_is_spectator && spectator_target) {
                if (observer_target == spectator_target) {
                    should_add = true;
                }
            }
            else {
                if (observer_target == g_ctx->m_local_pawn) {
                    should_add = true;
                }
            }

            if (!should_add)
                continue;

            std::string player_name = player.m_controller->m_sSanitizedPlayerName() ?
                player.m_controller->m_sSanitizedPlayerName() : "Unknown";
            uint64_t steam_id = player.m_controller->m_steamID();

            auto existing = std::find_if(persistent_observers.begin(), persistent_observers.end(),
                [&](const observer_info& info) { return info.player_name == player_name; });

            if (existing != persistent_observers.end()) {
                current_active.push_back(*existing);
            }
            else {
                observer_info new_observer = { player_name, steam_id, static_cast<float>(ImGui::GetTime()) };
                persistent_observers.push_back(new_observer);
                current_active.push_back(new_observer);

                m_observer_anim.observer_activation_times[player_name] = ImGui::GetTime();
                m_observer_anim.target_text_alphas[player_name] = 1.0f;
            }
        }

        if (local_is_spectator && spectator_target) {
            std::string local_name = g_ctx->m_local_controller->m_sSanitizedPlayerName() ?
                g_ctx->m_local_controller->m_sSanitizedPlayerName() : "You";
            uint64_t local_steam_id = g_ctx->m_local_controller->m_steamID();
            
            auto existing_local = std::find_if(persistent_observers.begin(), persistent_observers.end(),
                [&](const observer_info& info) { return info.player_name == local_name; });

            if (existing_local != persistent_observers.end()) {
                current_active.push_back(*existing_local);
            }
            else {
                observer_info local_observer = { local_name, local_steam_id, static_cast<float>(ImGui::GetTime()) };
                persistent_observers.push_back(local_observer);
                current_active.push_back(local_observer);

                m_observer_anim.observer_activation_times[local_name] = ImGui::GetTime();
                m_observer_anim.target_text_alphas[local_name] = 1.0f;
            }
        }
    }

    persistent_observers.erase(std::remove_if(persistent_observers.begin(), persistent_observers.end(),
        [&](const observer_info& info) {
            bool found = std::find_if(current_active.begin(), current_active.end(),
                [&](const observer_info& active) { return active.player_name == info.player_name; }) != current_active.end();

            if (!found)
                m_observer_anim.target_text_alphas[info.player_name] = 0.0f;
            return !found;
        }), persistent_observers.end());

    std::sort(current_active.begin(), current_active.end(),
        [](const observer_info& a, const observer_info& b) {
            return a.activation_time < b.activation_time;
        });

    bool has_active = !current_active.empty();
    m_observer_anim.has_active_observers = has_active;

    bool should_show_widget = has_active || is_menu_open;

    static bool prev_menu_state = false;
    static bool prev_has_active = false;

    if (has_active && !prev_has_active)
        m_observer_anim.target_widget_alpha = 1.0f;
    else if (!has_active && prev_has_active)
        m_observer_anim.target_widget_alpha = 0.0f;

    float lerp_speed = 8.0f * ImGui::GetIO().DeltaTime;

    if (is_menu_open) {
        if (!has_active)
            m_observer_anim.widget_alpha = 1.0f;
        else
            m_observer_anim.widget_alpha += (m_observer_anim.target_widget_alpha - m_observer_anim.widget_alpha) * lerp_speed;
    }
    else {
        if (has_active)
            m_observer_anim.widget_alpha += (m_observer_anim.target_widget_alpha - m_observer_anim.widget_alpha) * lerp_speed;
        else {
            if (prev_menu_state && !is_menu_open)
                m_observer_anim.widget_alpha = 0.0f;
            else
                m_observer_anim.widget_alpha += (m_observer_anim.target_widget_alpha - m_observer_anim.widget_alpha) * lerp_speed;
        }
    }

    prev_menu_state = is_menu_open;
    prev_has_active = has_active;

    if (!should_show_widget && m_observer_anim.widget_alpha < 0.01f) return;

    auto draw_list = ImGui::GetBackgroundDrawList();
    ImFont* font = g_font_manager->m_gheist_medium_14 ? g_font_manager->m_gheist_medium_14 : g_font_manager->m_inter_4002;
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;

    const float padding_x = 10.f;

    const float bar_height = 3.f;        
    const float header_height = 26.f;   
    const float item_height = 26.f;   
    const float base_width = 114;
    const float base_content_height = 18.f;



    const float avatar_size = 18.0f;
    const float avatar_padding = 8.0f;
    const float avatar_rounding = avatar_size * 0.5f;

    float max_width = base_width;

    for (const auto& observer : current_active) {
        ImVec2 name_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, observer.player_name.c_str());
        float total_width = name_size.x + padding_x * 2.f + avatar_size + avatar_padding;
        max_width = std::max(max_width, total_width);
    }

    m_observer_anim.target_widget_width = max_width;
    m_observer_anim.widget_width = max_width;

    float content_height = has_active ? (current_active.size() * item_height) : (base_content_height * 0.5f);
    float total_height = header_height + bar_height + content_height;
    m_observer_anim.target_base_rect_height = total_height;

    m_observer_anim.base_rect_height += (m_observer_anim.target_base_rect_height - m_observer_anim.base_rect_height) * lerp_speed;

    static bool position_initialized = false;
    if (!position_initialized || m_observer_anim.widget_pos.x < 0.0f || m_observer_anim.widget_pos.y < 0.0f) {
        m_observer_anim.widget_pos = ImVec2(screen_size.x - 180.f, screen_size.y * 0.55f);
        position_initialized = true;
    }

    if (is_menu_open) {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        ImVec2 rect_min = m_observer_anim.widget_pos;
        ImVec2 rect_max = ImVec2(rect_min.x + m_observer_anim.widget_width, rect_min.y + m_observer_anim.base_rect_height);

        bool hovered = mouse_pos.x >= rect_min.x && mouse_pos.x <= rect_max.x && mouse_pos.y >= rect_min.y && mouse_pos.y <= rect_max.y;

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_observer_anim.dragging = true;
            m_observer_anim.drag_offset = ImVec2(mouse_pos.x - rect_min.x, mouse_pos.y - rect_min.y);
        }

        if (m_observer_anim.dragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_observer_anim.widget_pos = ImVec2(mouse_pos.x - m_observer_anim.drag_offset.x,
                    mouse_pos.y - m_observer_anim.drag_offset.y);
            }
            else
                m_observer_anim.dragging = false;
        }
    }

 
    ImVec2 header_min = m_observer_anim.widget_pos;
    ImVec2 header_max = ImVec2(m_observer_anim.widget_pos.x + m_observer_anim.widget_width, m_observer_anim.widget_pos.y + header_height);

    hellcolor header_bg_color(28, 28, 30, 255);
    draw_list->AddRectFilled(header_min, header_max, header_bg_color, 0, ImDrawFlags_RoundCornersTop);


    ImVec2 header_text_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, "Spectators");
    float header_width = header_max.x - header_min.x;
    float text_x = header_min.x + (header_width - header_text_size.x) * 0.5f;
    float text_y = header_min.y + (header_height - header_text_size.y) * 0.5f;
    ImVec2 header_text_pos = ImVec2(text_x, text_y);

    hellcolor header_text_color(255, 255, 255, 255);
    draw_list->AddText(font, font->LegacySize, header_text_pos, header_text_color, "Spectators");
    hellcolor grad_edge(20, 25, 50, 255);       
    hellcolor grad_purple(60, 45, 100, 255);    
    hellcolor grad_glow(120, 90, 180, 255);    
    ImVec2 bar_min = ImVec2(header_min.x, header_max.y);
    ImVec2 bar_max = ImVec2(header_max.x, header_max.y + bar_height);

    float bar_w = bar_max.x - bar_min.x;
    float x0 = bar_min.x;
    float x_35 = bar_min.x + bar_w * 0.35f;
    float x_50 = bar_min.x + bar_w * 0.50f;
    float x_65 = bar_min.x + bar_w * 0.65f;
    float x_100 = bar_max.x;

    // edge -> purple (0% .. 35%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x0, bar_min.y), ImVec2(x_35, bar_max.y),
        grad_edge, grad_purple, grad_purple, grad_edge
    );
    // purple -> glow (35% .. 50%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_35, bar_min.y), ImVec2(x_50, bar_max.y),
        grad_purple, grad_glow, grad_glow, grad_purple
    );
    // glow -> purple (50% .. 65%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_50, bar_min.y), ImVec2(x_65, bar_max.y),
        grad_glow, grad_purple, grad_purple, grad_glow
    );
    // purple -> edge (65% .. 100%)
    draw_list->AddRectFilledMultiColor(
        ImVec2(x_65, bar_min.y), ImVec2(x_100, bar_max.y),
        grad_purple, grad_edge, grad_edge, grad_purple
    );


    ImVec2 content_min = ImVec2(m_observer_anim.widget_pos.x, bar_max.y);
    ImVec2 content_max = ImVec2(m_observer_anim.widget_pos.x + m_observer_anim.widget_width, m_observer_anim.widget_pos.y + m_observer_anim.base_rect_height);

    if (hell::blur::m_initialized) {
        ImVec2 rect_size = ImVec2(content_max.x - content_min.x, content_max.y - content_min.y);
        hell::blur::blur_effect.begin_blur();
        hell::blur::blur_effect.apply_blur(draw_list, content_min, rect_size, 0.0f, 6.0f, ImDrawFlags_RoundCornersBottom);
        hell::blur::blur_effect.end_blur();
    }

    hellcolor content_bg_color(20, 20, 22, (int)(150 * m_observer_anim.widget_alpha));
    draw_list->AddRectFilled(content_min, content_max, content_bg_color, 0, ImDrawFlags_RoundCornersBottom);

    if (has_active) {
        draw_list->PushClipRect(content_min, content_max, true);
        float current_y = content_min.y;

        for (const auto& observer : current_active) {
            if (m_observer_anim.text_alphas.find(observer.player_name) == m_observer_anim.text_alphas.end())
                m_observer_anim.text_alphas[observer.player_name] = 0.0f;

            if (m_observer_anim.target_text_alphas.find(observer.player_name) == m_observer_anim.target_text_alphas.end())
                m_observer_anim.target_text_alphas[observer.player_name] = 1.0f;

            float& alpha = m_observer_anim.text_alphas[observer.player_name];
            float target_alpha = m_observer_anim.target_text_alphas[observer.player_name];
            alpha += (target_alpha - alpha) * lerp_speed;

            if (alpha > 0.01f) {
                ImTextureID avatar = (ImTextureID)0;
                if (observer.steam_id != 0)
                    avatar = get_avatar(observer.steam_id);

                ImVec2 avatar_pos = ImVec2(content_min.x + padding_x, current_y + (item_height - avatar_size) * 0.5f);
                ImVec2 avatar_max = ImVec2(avatar_pos.x + avatar_size, avatar_pos.y + avatar_size);

                if (avatar) {
                    ImU32 avatar_color = IM_COL32(255, 255, 255, (int)(255 * alpha * m_observer_anim.widget_alpha));

                    draw_list->PushClipRect(avatar_pos, avatar_max, true);
                    draw_list->AddImageRounded(avatar, avatar_pos, avatar_max, ImVec2(0, 0), ImVec2(1, 1), avatar_color, avatar_rounding);
                    draw_list->PopClipRect();
                }
                else {
                    ImVec2 avatar_center = ImVec2(avatar_pos.x + avatar_size * 0.5f, avatar_pos.y + avatar_size * 0.5f);

                    hellcolor base_color(90, 28, 38, (int)(255 * alpha * m_observer_anim.widget_alpha));
                    draw_list->AddCircleFilled(avatar_center, avatar_size * 0.5f, base_color, 16);

                    hellcolor highlight_color(150, 55, 65, (int)(180 * alpha * m_observer_anim.widget_alpha));
                    ImVec2 highlight_center = ImVec2(avatar_center.x - avatar_size * 0.12f, avatar_center.y - avatar_size * 0.12f);
                    draw_list->AddCircleFilled(highlight_center, avatar_size * 0.22f, highlight_color, 12);
                }

                float text_x_offset = avatar_size + avatar_padding;



                hellcolor name_color(255, 255, 255, (int)(255 * alpha * m_observer_anim.widget_alpha));

                float max_text_width = content_max.x - content_min.x - padding_x * 2.f - avatar_size - avatar_padding;
                std::string display_name = observer.player_name;
                ImVec2 name_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, display_name.c_str());
                ImVec2 name_pos = ImVec2(content_min.x + padding_x + text_x_offset, current_y + (item_height - name_size.y) * 0.5f);

                if (name_size.x > max_text_width) {
                    display_name = observer.player_name;
                    while (!display_name.empty()) {
                        display_name.pop_back();
                        ImVec2 test_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, (display_name + "...").c_str());
                        if (test_size.x <= max_text_width) {  
                            display_name += "...";
                            break;
                        }
                    }
                    if (display_name.empty())
                        display_name = "...";
                    name_size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.f, display_name.c_str());
                }

    
                if (name_size.x <= max_text_width) {
                    draw_list->AddText(font, font->LegacySize, name_pos, name_color, display_name.c_str());
                }
            }

            current_y += item_height;
        }
        draw_list->PopClipRect();
    }
}
ImTextureID c_overlay::get_avatar(uint64_t steam_id) {
    if (avatar_cache.find(steam_id) != avatar_cache.end())
        return avatar_cache[steam_id];

    int iImage = g_interfaces->m_steam_friends->GetMediumFriendAvatar(steam_id);
    if (iImage == -1)
        iImage = g_interfaces->m_steam_friends->GetLargeFriendAvatar(steam_id);
    if (iImage == -1)
        return {};

    uint32 uAvatarWidth, uAvatarHeight;
    if (!g_interfaces->m_steam_utils->GetImageSize(iImage, &uAvatarWidth, &uAvatarHeight))
        return {};

    const int uImageSizeInBytes = uAvatarWidth * uAvatarHeight * 4;
    std::vector<uint8> avatar_rgba(uImageSizeInBytes);
    if (!g_interfaces->m_steam_utils->GetImageRGBA(iImage, avatar_rgba.data(), avatar_rgba.size()))
        return {};

    ID3D11ShaderResourceView* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = uAvatarWidth;
    desc.Height = uAvatarHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub_resource = {};
    sub_resource.pSysMem = avatar_rgba.data();
    sub_resource.SysMemPitch = uAvatarWidth * 4;

    ID3D11Texture2D* texture2d = nullptr;
    if (FAILED(g_interfaces->m_device->CreateTexture2D(&desc, &sub_resource, &texture2d)))
        return {};

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = desc.MipLevels;

    if (FAILED(g_interfaces->m_device->CreateShaderResourceView(texture2d, &srv_desc, &texture))) {
        texture2d->Release();
        return {};
    }

    texture2d->Release();

    ImTextureID avatar = reinterpret_cast<ImTextureID>(texture);
    avatar_cache[steam_id] = avatar;
    return avatar;
}