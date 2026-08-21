#include "skybox_changer.h"
#include <sdk/interfaces/resource_system.h>
#include <core/interfaces/interfaces.h>
#include "chams.h"
#include <sdk/datatypes/key_values.h>
#include <sdk/datatypes/c_buffer_string.h>
#include "skybox_changer.h"
#include "chams.h"
#include <sdk/interfaces/resource_system.h>
#include <sdk/interfaces/engine_client.h>
#include <core/interfaces/interfaces.h>

#include <context.h>

inline const char* skybox_template = R"(
        <!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
        {
            Shader = "sky.vfx"
            g_flBrightnessExposureBias = 0.000000
            g_flRenderOnlyExposureBias = 0.000000
            SkyTexture = resource:"%s"
            g_tSkyTexture = resource:"%s"
        }
)";

namespace {
	void restore_default_skybox_safe(c_skybox_changer* self) {
		if (!self)
			return;

		__try {
			self->restore_default_skybox();
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			self->reset_after_error();
		}
	}

	__declspec(noinline) void restore_skybox_swaps(
		c_skybox_changer::skybox_material_swap_t* swaps,
		size_t count)
	{
		__try {
			for (size_t i = 0; i < count; ++i) {
				auto& swap = swaps[i];
				if (!swap.slot)
					continue;
				if (!swap.original)
					continue;

				c_material_2* current = *swap.slot;
				if (!is_valid_custom_skybox_material(current))
					continue;

				*swap.slot = swap.original;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}
auto create_material = [](const char* material_name, const char vmat[]) -> c_strong_handle<c_material_2> {
	if (!material_name || !vmat)
		return {};

	c_key_values_3* keyval = c_key_values_3::create_material_resource();
	if (!keyval)
		return {};

	keyval->load_from_buffer(vmat);

	c_strong_handle<c_material_2> custom_material{};
	using fn_create_material = __int64(__fastcall*)(void*, void*, const char*, c_key_values_3*, unsigned int, unsigned int);
	static fn_create_material create_material = g_modules->m_materialsystem2.find(xx("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 8B F2")).as<fn_create_material>();

	if (!create_material)
		return {};

	create_material(nullptr, &custom_material, material_name, keyval, 0, 1);

	return custom_material;
	};
void c_skybox_changer::restore_swapped_materials() {
	if (!m_material_swaps.empty())
		restore_skybox_swaps(m_material_swaps.data(), m_material_swaps.size());

	m_material_swaps.clear();
}

void c_skybox_changer::apply_custom_skybox_materials() {
	if (!m_custom_skybox_material || !g_interfaces || !g_interfaces->m_resource_system)
		return;

	restore_swapped_materials();

	material_array_t mat_arr = {};
	g_interfaces->m_resource_system->enumerate_materials(0x74616D76, &mat_arr, 2);

	if (!mat_arr.m_material_resource || mat_arr.m_count <= 0)
		return;

	for (int i = 0; i < mat_arr.m_count; ++i) {
		if (!mat_arr.m_material_resource[i])
			continue;

		c_material_2** material_resource = mat_arr.m_material_resource[i];
		if (!material_resource)
			continue;

		c_material_2* material = *material_resource;
		if (!material)
			continue;

		const char* material_name = material->get_name(); // <-- нужен реальный геттер имени, см. ниже
		if (!material_name)
			continue;

		if (strstr(material_name, "materials/skybox") == nullptr)
			continue;

		m_material_swaps.push_back({ material_resource, material });
		*material_resource = m_custom_skybox_material;
	}
}

void c_skybox_changer::refresh_custom_skyboxes() {
	m_custom_skyboxes.clear();

	if (!g_ctx)
		return;

	std::error_code ec;
	const std::filesystem::path base_path = std::filesystem::path(g_ctx->m_running_path)
		.parent_path().parent_path().parent_path() / "csgo" / "materials" / "skybox";
	if (!std::filesystem::exists(base_path, ec))
		return;

	for (const auto& entry : std::filesystem::directory_iterator(base_path, ec)) {
		if (ec)
			break;

		if (!entry.is_regular_file(ec))
			continue;

		const auto path = entry.path();
		if (path.extension() == ".vtex_c")
			m_custom_skyboxes.push_back(path.stem().string());
	}
}

std::vector<std::string> c_skybox_changer::get_all_skyboxes() {
	std::vector<std::string> result;

	for (int i = 0; i < k_skybox_combo_count; ++i)
		result.emplace_back(arr_skybox_names[i]);

	result.insert(result.end(), m_custom_skyboxes.begin(), m_custom_skyboxes.end());
	return result;
}

void c_skybox_changer::run() {
	
	if (!g_interfaces || !g_interfaces->m_engine
		|| !g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
		return;

	if (!g_ctx)
		return;

	if (!GET_VAR(bool, VISUALS_PATH(m_enable_custom_sky))) {
		if (m_has_custom_skybox)
			restore_default_skybox_safe(this);

		m_has_custom_skybox = false;
		m_custom_skybox_material = nullptr;
		m_custom_skybox_material_handle = {};
		m_last_selected_skybox = -1;
		return;
	}

	int current_idx = GET_VAR(int, VISUALS_PATH(m_selected_skybox));
	if (current_idx < 0)
		current_idx = 0;

	if (m_last_selected_skybox == current_idx && !m_need_update_material)
		return;

	m_last_selected_skybox = current_idx;
	m_need_update_material = false;
	m_has_custom_skybox = false;
	m_custom_skybox_material = nullptr;
	m_custom_skybox_material_handle = {};

	if (current_idx == 0) {
		restore_default_skybox_safe(this);
		return;
	}

	const bool is_custom = current_idx >= k_custom_skybox_index_offset;
	std::string selected_vtex;

	if (is_custom) {
		const int custom_idx = current_idx - k_custom_skybox_index_offset;
		if (custom_idx < 0 || custom_idx >= static_cast<int>(m_custom_skyboxes.size()))
			return;

		selected_vtex = "materials/skybox/" + m_custom_skyboxes[custom_idx] + ".vtex";
	}
	else if (current_idx >= 1 && current_idx <= k_builtin_skybox_count) {
		selected_vtex = std::string(arr_skybox_paths[current_idx - 1]);
	}
	else {
		return;
	}

	if (!g_interfaces->m_resource_system)
		return;

	c_buffer_string buffer_vtex(selected_vtex.c_str(), 'xetv');
	g_interfaces->m_resource_system->blocking_load_resource_by_name(&buffer_vtex, "");

	char formatted_material[2048];
	sprintf_s(formatted_material, skybox_template, selected_vtex.c_str(), selected_vtex.c_str());

	m_custom_skybox_material_handle = create_material("our_own_skybox", formatted_material);
	m_custom_skybox_material = m_custom_skybox_material_handle;

	if (!is_valid_custom_skybox_material(m_custom_skybox_material))
		return;

	m_has_custom_skybox = true;
	apply_custom_skybox_materials();
}

void c_skybox_changer::reset_for_level_load() {
	restore_swapped_materials();
	m_has_custom_skybox = false;
	m_custom_skybox_material = nullptr;
	m_custom_skybox_material_handle = {};
	m_default_skybox_material = nullptr;
	m_need_update_material = true;
	m_pending_level_init = true;
	m_last_selected_skybox = -1;
}

void c_skybox_changer::ensure_level_resources() {
	if (!m_pending_level_init)
		return;

	if (!g_interfaces || !g_interfaces->m_engine
		|| !g_interfaces->m_engine->is_connected()
		|| !g_interfaces->m_engine->in_game())
		return;



	init_on_level_load();
	m_pending_level_init = false;
}

void c_skybox_changer::init_on_level_load() {
	if (!g_interfaces->m_resource_system)
		return;

	m_has_custom_skybox = false;
	m_custom_skybox_material = nullptr;
	m_custom_skybox_material_handle = {};
	m_default_skybox_material = nullptr;

	__try {
		material_array_t mat_arr = {};
		g_interfaces->m_resource_system->enumerate_materials(0x74616D76, &mat_arr, 2);

	
		for (int i = 0; i < mat_arr.m_count; ++i) {
			if (!mat_arr.m_material_resource[i])
				continue;

			auto material_resource = mat_arr.m_material_resource[i];


			auto material = *material_resource;
			if (!material)
				continue;

			const char* material_name = nullptr;
		

			if (strstr(material_name, "materials/skybox") != nullptr && strstr(material_name, "our_own_skybox") == nullptr) {
				m_default_skybox_material = material;
				break;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		m_default_skybox_material = nullptr;
	}
}

void c_skybox_changer::restore_default_skybox() {
	restore_swapped_materials();

	m_has_custom_skybox = false;
	m_custom_skybox_material = nullptr;
	m_custom_skybox_material_handle = {};
}

void c_skybox_changer::abandon_material_state() {
	m_material_swaps.clear();
	m_has_custom_skybox = false;
	m_custom_skybox_material = nullptr;
	m_custom_skybox_material_handle = {};
	m_default_skybox_material = nullptr;
}

void c_skybox_changer::prepare_level_shutdown() {
	restore_swapped_materials();
	abandon_material_state();
}

void c_skybox_changer::cleanup() {
	abandon_material_state();
	m_last_selected_skybox = -1;
	m_need_update_material = true;
	m_pending_level_init = false;
}

void c_skybox_changer::reset_after_error() {
	abandon_material_state();
}
