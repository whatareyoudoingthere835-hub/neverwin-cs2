#pragma once
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <cstring>
#include <sdk/interfaces/material_system.h>


inline bool is_valid_custom_skybox_material(c_material_2* material) {
	if (!material)
		return false;

	const char* name = material->get_name(); 
	if (!name)
		return false;

	return strstr(name, "our_own_skybox") != nullptr;
}

static constexpr int k_builtin_skybox_count = 12;
static constexpr int k_skybox_combo_count = 13;
static constexpr int k_custom_skybox_index_offset = 13;

static const char* arr_skybox_names[] = {
	"Default",
	"Sunset",
	"Overcast",
	"Night",
	"Aztec",
	"Jungle",
	"Cloudy",
	"Overpass",
	"Nuke",
	"Office",
	"Anubis",
	"Train",
	"Vertigo"
};

static constexpr std::array<std::string_view, k_builtin_skybox_count> arr_skybox_paths = {
	"materials/skybox/cs_italy_s2_skybox_sunset_2_exr_e56cedf6.vtex",
	"materials/skybox/sky_overcast_01_exr_da4019b1.vtex",
	"materials/skybox/tests/src/lightingtest_sky_night_exr_2c5e8c62.vtex",
	"materials/skybox/sky_hr_aztec_02_exr_f84f8de9.vtex",
	"materials/skybox/jungle_cube_pfm_bc16d813.vtex",
	"materials/skybox/sky_csgo_cloudy01_cube_pfm_f9a0b177.vtex",
	"materials/skybox/sky_de_overpass_01_exr_f8534391.vtex",
	"materials/skybox/sky_de_nuke_exr_f04e84b2.vtex",
	"materials/skybox/sky_cs_office_45_0_exr_d0152542.vtex",
	"materials/skybox/sky_de_annubis_exr_2c5e0b53.vtex",
	"materials/skybox/sky_de_train03_exr_4fdb8a38.vtex",
	"materials/skybox/sky_de_vertigo_exr_c70a3937.vtex"
};


class c_skybox_changer {
public:
	struct skybox_material_swap_t {
		c_material_2** slot = nullptr;
		c_material_2* original = nullptr;
	};

	void run();
	void refresh_custom_skyboxes();
	std::vector<std::string> get_all_skyboxes();
	void init_on_level_load();
	void reset_for_level_load();
	void ensure_level_resources();
	void restore_default_skybox();
	void prepare_level_shutdown();
	void cleanup();
	void reset_after_error();
	void abandon_material_state();

	bool m_need_update_material = false;
	bool m_pending_level_init = false;
	c_material_2* m_default_skybox_material = nullptr;
	c_strong_handle<c_material_2> m_custom_skybox_material_handle{};
	c_material_2* m_custom_skybox_material = nullptr;
	bool m_has_custom_skybox = false;
	int m_last_selected_skybox = -1;
private:
	void restore_swapped_materials();
	void apply_custom_skybox_materials();

	std::vector<std::string> m_custom_skyboxes;
	std::vector<skybox_material_swap_t> m_material_swaps;
};

inline auto g_skybox_changer = std::make_unique<c_skybox_changer>();
