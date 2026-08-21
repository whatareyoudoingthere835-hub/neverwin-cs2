#include "skins.h"

#include <cheat/config/vars.h>
#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <Windows.h>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;
namespace {
    struct econ_item_attribute_t {
        char     pad_0000[0x30];
        uint16_t def_index;
        char     pad_0032[2];
        float    value;
        float    init_value;
        int32_t  refundable_currency;
        bool     set_bonus;
        char     pad_0041[7];
    };
    struct ptr_game_vector_t { uint64_t size; uintptr_t ptr; };

    enum : uint16_t { ATTR_PAINT = 6, ATTR_PATTERN = 7, ATTR_WEAR = 8 };

    void* game_alloc(size_t n) {
        static auto fn = reinterpret_cast<void* (__fastcall*)(size_t)>(
            GetProcAddress(GetModuleHandleA("tier0.dll"), "MemAlloc_AllocFunc"));
        return fn ? fn(n) : nullptr;
    }
    void game_free(void* p) {
        static auto fn = reinterpret_cast<void(__fastcall*)(void*)>(
            GetProcAddress(GetModuleHandleA("tier0.dll"), "MemAlloc_FreeFunc"));
        if (p && fn) fn(p);
    }

    void set_glove_bodygroup(void* pawn) {
        using fn_t = void(__fastcall*)(void*, const char*, unsigned int);
        static fn_t fn = []() -> fn_t {
            auto a = g_modules->m_client.find("E8 ? ? ? ? EB 0C 48 8B CF");
            if (!a.raw())
                return nullptr;
            return a.absolute(1, 0).as<fn_t>();
        }();
        if (fn)
            fn(pawn, "first_or_third_person", 1);
    }

    ptr_game_vector_t* attr_vec(c_econ_item_view* item) {
        static const uint16_t list_off = [] {
            g_schema->init_class("C_EconItemView");
            return g_schema->get_offset("C_EconItemView->m_AttributeList");
        }();
        static const uint16_t attrs_off = [] {
            g_schema->init_class("CAttributeList");
            return g_schema->get_offset("CAttributeList->m_Attributes");
        }();
        if (!item || list_off == 0)
            return nullptr;
        return reinterpret_cast<ptr_game_vector_t*>(reinterpret_cast<uintptr_t>(item) + list_off + attrs_off);
    }
   
    void attr_remove(c_econ_item_view* item) {
        auto* vec = attr_vec(item);
        if (!vec)
            return;
        __try {
            if (vec->size == 0 || vec->size > 64)
                return;
            void* p = reinterpret_cast<void*>(vec->ptr);
            vec->size = 0;
            vec->ptr = 0;
            game_free(p);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void attr_create(c_econ_item_view* item, int paint_kit, float wear, int seed) {
        if (paint_kit <= 0)
            return;
        auto* vec = attr_vec(item);
        if (!vec)
            return;
        __try {
            if (vec->size != 0 || vec->ptr != 0)
                return;
            constexpr size_t attr_count = 3;
            auto* attrs = static_cast<econ_item_attribute_t*>(game_alloc(attr_count * sizeof(econ_item_attribute_t)));
            if (!attrs)
                return;
            memset(attrs, 0, attr_count * sizeof(econ_item_attribute_t));
            attrs[0].def_index = ATTR_PAINT;   attrs[0].value = static_cast<float>(paint_kit);          attrs[0].init_value = attrs[0].value;
            attrs[1].def_index = ATTR_PATTERN; attrs[1].value = static_cast<float>(seed >= 0 ? seed : 0); attrs[1].init_value = attrs[1].value;
            attrs[2].def_index = ATTR_WEAR;    attrs[2].value = wear >= 0.0f ? wear : 0.01f;             attrs[2].init_value = attrs[2].value;
            vec->size = attr_count;
            vec->ptr = reinterpret_cast<uintptr_t>(attrs);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static const std::unordered_map<int, uint32_t> k_knife_subclass_hashes = {
    {500, 3933374535u}, {503, 3787235507u}, {505, 4046390180u}, {506, 2047704618u},
    {507, 1731408398u}, {508, 1638561588u}, {509, 2282479884u}, {512, 3412259219u},
    {514, 2511498851u}, {515, 1353709123u}, {516, 4269888884u}, {517, 1105782941u},
    {518, 275962944u},  {519, 1338637359u}, {520, 3230445913u}, {521, 3206681373u},
    {522, 2595277776u}, {523, 4029975521u}, {525, 365028728u},  {526, 3845286452u},
};

static const std::unordered_map<int, const char*> k_fallback_knife_models = {
    {500, "phase2/weapons/models/knife/knife_bayonet/weapon_knife_bayonet_ag2.vmdl"},
    {503, "phase2/weapons/models/knife/knife_css/weapon_knife_css_ag2.vmdl"},
    {505, "phase2/weapons/models/knife/knife_flip/weapon_knife_flip_ag2.vmdl"},
    {506, "phase2/weapons/models/knife/knife_gut/weapon_knife_gut_ag2.vmdl"},
    {507, "phase2/weapons/models/knife/knife_karambit/weapon_knife_karambit_ag2.vmdl"},
    {508, "phase2/weapons/models/knife/knife_m9/weapon_knife_m9_ag2.vmdl"},
    {509, "phase2/weapons/models/knife/knife_tactical/weapon_knife_tactical_ag2.vmdl"},
    {512, "phase2/weapons/models/knife/knife_falchion/weapon_knife_falchion_ag2.vmdl"},
    {514, "phase2/weapons/models/knife/knife_bowie/weapon_knife_bowie_ag2.vmdl"},
    {515, "phase2/weapons/models/knife/knife_butterfly/weapon_knife_butterfly_ag2.vmdl"},
    {516, "phase2/weapons/models/knife/knife_push/weapon_knife_push_ag2.vmdl"},
    {517, "phase2/weapons/models/knife/knife_cord/weapon_knife_cord_ag2.vmdl"},
    {518, "phase2/weapons/models/knife/knife_canis/weapon_knife_canis_ag2.vmdl"},
    {519, "phase2/weapons/models/knife/knife_ursus/weapon_knife_ursus_ag2.vmdl"},
    {520, "phase2/weapons/models/knife/knife_navaja/weapon_knife_navaja_ag2.vmdl"},
    {521, "phase2/weapons/models/knife/knife_outdoor/weapon_knife_outdoor_ag2.vmdl"},
    {522, "phase2/weapons/models/knife/knife_stiletto/weapon_knife_stiletto_ag2.vmdl"},
    {523, "phase2/weapons/models/knife/knife_talon/weapon_knife_talon_ag2.vmdl"},
    {525, "phase2/weapons/models/knife/knife_skeleton/weapon_knife_skeleton_ag2.vmdl"},
    {526, "phase2/weapons/models/knife/knife_kukri/weapon_knife_kukri_ag2.vmdl"},
};

static bool is_paint_kit_from_weapon(c_paint_kit* paint_kit, const char* weapon_id)
{
    auto path = "panorama/images/econ/default_generated/" + std::string(weapon_id) + "_" + paint_kit->paint_kit_name() + "_light_png.vtex_c";
    return g_interfaces->m_file_system->exists(path.c_str(), "GAME");
}

std::vector<c_dumped_skin> c_skins::process_paint_kits(c_econ_item_schema* econ_item_schema, c_econ_item_definition* item)
{
    std::vector<c_dumped_skin> skins;
    for (const auto& paint_kit_entry : econ_item_schema->get_paint_kits())
    {
        c_paint_kit* paint_kit = paint_kit_entry.m_value;
        if (paint_kit && paint_kit->paint_kit_id() != 0 && paint_kit->paint_kit_id() != 9001)
        {
            if (!is_paint_kit_from_weapon(paint_kit, item->get_simple_weapon_name()))
                continue;

            uint32_t item_rarity = item->m_iterarity;
            uint32_t paint_kit_rarity = paint_kit->paint_kit_rarity();
            uint32_t final_rarity = std::clamp((int)(item_rarity + paint_kit_rarity - 1), 0, (paint_kit_rarity == 7) ? 7 : 6);

            skins.emplace_back(c_dumped_skin{
                g_interfaces->m_localize->find_safe(paint_kit->paint_kit_description_tag()),
                (int)paint_kit->paint_kit_id(),
                final_rarity
                });
        }
    }
    return skins;
}

void c_skins::add_item_to_category(c_econ_item_schema* econ_item_schema, c_econ_item_definition* item, bool is_special_item, std::vector<c_dumped_item>& category)
{
    c_dumped_item dumped_item(
        g_interfaces->m_localize->find_safe(item->m_item_base_name),
        item->get_item_definition_index(),
        is_special_item
    );

    dumped_item.skins = process_paint_kits(econ_item_schema, item);

    if (!dumped_item.skins.empty())
        std::sort(dumped_item.skins.begin(), dumped_item.skins.end(), [](const c_dumped_skin& a, const c_dumped_skin& b) {
        return a.rarity > b.rarity;
            });

    category.emplace_back(dumped_item);
}
void c_skins::agent_changer()
{
    if (!g_ctx->m_local_pawn || g_ctx->m_local_pawn->m_iHealth() <= 0)
        return;

    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;

    int team_num = g_ctx->m_local_pawn->m_iTeamNum();
    const bool is_ct = team_num == 3, is_t = team_num == 2;

    int menu_selected_ct = GET_VAR(int, SKINS_PATH(m_agent_selected_ct));
    int menu_selected_t = GET_VAR(int, SKINS_PATH(m_agent_selected_t));

    if (agents.agent_models.empty())
        return;

    if (is_ct)
        g_ctx->m_local_pawn->set_model(agents.agent_models.at(menu_selected_ct));
    else if (is_t)
       g_ctx->m_local_pawn->set_model(agents.agent_models.at(menu_selected_t));
}
void c_skins::dump_items()
{
    auto econ_item_schema = g_interfaces->m_source2_client->get_econ_item_system()->get_econ_item_schema();
    if (!econ_item_schema || !items.empty())
        return;

    for (const auto& it : econ_item_schema->get_sorted_item_definition_map())
    {
        c_econ_item_definition* item = it.m_value;
        if (!item || !item->m_item_base_name || item->m_item_base_name[0] == '\0')
            continue;

        if (item->is_agent())
        {
            agents.agent_models.emplace_back(item->get_model_name());
            agents.agent_names.emplace_back(g_interfaces->m_localize->find_safe(item->m_item_base_name));
            agents.agent_ids.emplace_back(item->get_item_definition_index());
        }
        else if (item->is_knife(true))
        {
            const int def_index = item->get_item_definition_index();
            if (k_knife_subclass_hashes.find(def_index) == k_knife_subclass_hashes.end())
                continue;

            const char* model = item->get_model_name();
            if (!model || model[0] == '\0') {
                auto fb = k_fallback_knife_models.find(def_index);
                model = (fb != k_fallback_knife_models.end()) ? fb->second : model;
            }

            knives.models.emplace_back(model);
            knives.names.emplace_back(g_interfaces->m_localize->find_safe(item->m_item_base_name));
            knives.indexes.emplace_back(def_index);

            add_item_to_category(econ_item_schema, item, true, knives_items);
        }
        else if (item->is_glove(true))
        {
            gloves.models.emplace_back(item->get_model_name());
            gloves.names.emplace_back(g_interfaces->m_localize->find_safe(item->m_item_base_name));
            gloves.indexes.emplace_back(item->get_item_definition_index());

            add_item_to_category(econ_item_schema, item, true, glove_items);
        }
        else if (item->is_weapon())
        {
            add_item_to_category(econ_item_schema, item, false, items);
        }
    }

    std::vector<std::tuple<const char*, const char*, int, uint32_t>> agent_data;
    for (size_t i = 0; i < agents.agent_ids.size(); ++i) {
        auto item_def = econ_item_schema->get_sorted_item_definition_map().find_by_key(agents.agent_ids[i]);
        uint32_t rarity = item_def.has_value() ? item_def.value()->m_iterarity : 0;
        agent_data.emplace_back(agents.agent_models[i], agents.agent_names[i], agents.agent_ids[i], rarity);
    }

    std::sort(agent_data.begin(), agent_data.end(), [](const auto& a, const auto& b) {
        return std::get<3>(a) > std::get<3>(b);
        });

    agents.agent_models.clear();
    agents.agent_names.clear();
    agents.agent_ids.clear();

    for (const auto& data : agent_data) {
        agents.agent_models.emplace_back(std::get<0>(data));
        agents.agent_names.emplace_back(std::get<1>(data));
        agents.agent_ids.emplace_back(std::get<2>(data));
    }

    g_ctx->m_dumped_skins = true;
}

// Используем CBufferString для передачи пути
struct CBufferString {
    int m_nLength;
    int m_nAllocatedSize;
    union {
        char* m_pString;
        char m_szString[8];
    };

    CBufferString() : m_nLength(0), m_nAllocatedSize(0x80000000 | 0x40000000 | 8) {
        m_pString = nullptr;
    }

    void Insert(int index, const char* str, int length, bool b1) {
        static auto fn = (const char* (__fastcall*)(void*, int, const char*, int, bool))
            GetProcAddress(GetModuleHandleA("tier0.dll"), "?Insert@CBufferString@@QEAAPEBDHPEBDH_N@Z");
        if (fn) fn(this, index, str, length, b1);
    }
};



#define TOLOWERU(c) ((uint32_t)(((c >= 'A') && (c <= 'Z')) ? c + 32 : c))
#define STRINGTOKEN_MURMURHASH_SEED 0x31415926

inline uint32_t MurmurHash2LowerCaseA1(const char* pString, int len, uint32_t nSeed)
{
    char* p = (char*)malloc(len + 1);
    for (int i = 0; i < len; i++)
    {
        p[i] = TOLOWERU(pString[i]);
    }
    return MurmurHash2(p, len, nSeed);
}

class c_utl_string_token
{
public:
    std::uint32_t m_nHashCode;
#if DEBUG_STRINGTOKENS
    const char* m_pDebugName;
#endif

    c_utl_string_token(const char* szString)
    {
        this->SetHashCode1(this->MakeStringToken1(szString));
    }

    bool operator==(const c_utl_string_token& other) const
    {
        return (other.m_nHashCode == m_nHashCode);
    }

    bool operator!=(const c_utl_string_token& other) const
    {
        return (other.m_nHashCode != m_nHashCode);
    }

    bool operator<(const c_utl_string_token& other) const
    {
        return (m_nHashCode < other.m_nHashCode);
    }

    uint32_t GetHashCode1(void) const
    {
        return m_nHashCode;
    }

    void SetHashCode1(uint32_t nCode)
    {
        m_nHashCode = nCode;
    }

    __forceinline std::uint32_t MakeStringToken1(const char* szString, int nLen)
    {
        std::uint32_t nHashCode = MurmurHash2LowerCaseA1(szString, nLen, STRINGTOKEN_MURMURHASH_SEED);
        return nHashCode;
    }

    __forceinline std::uint32_t MakeStringToken1(const char* szString)
    {
        return MakeStringToken1(szString, (int)strlen(szString));
    }

    //__forceinline std::uint32_t MakeStringToken(CUtlString& str)
    //{
    //    return MakeStringToken(str.Get(), str.Length());
    //}

    c_utl_string_token()
    {
        m_nHashCode = 0;
    }
};

void c_skins::knifes(int stage)
{
    if (!g_ctx->m_local_pawn || !g_ctx->m_local_controller)
        return;

    auto weapon_services = g_ctx->m_local_pawn->m_pWeaponServices();
    if (!weapon_services)
        return;

    c_base_view_model* view_model = g_ctx->m_local_pawn->get_view_model_brutforce();
    if (!view_model)
        return;

    c_network_utl_vector<c_base_handle>& my_weapons = weapon_services->m_hMyWeapons();

    for (auto i = 0; i < my_weapons.size; i++)
    {
        c_econ_entity* weapon = reinterpret_cast<c_econ_entity*>(g_interfaces->m_entity_system->get_base_entity(my_weapons.elements[i].get_entry_index()).as<void*>());
        if (!weapon)
            continue;

        c_attribute_container* attribute_manager = weapon->m_AttributeManager();
        if (!attribute_manager)
            continue;

        c_econ_item_view* item = attribute_manager->m_Item();
        if (!item)
            continue;

        const uint16_t cur_def = item->m_iItemDefinitionIndex();
        const bool is_knife_entity = (cur_def == 42 || cur_def == 59 || (cur_def >= 500 && cur_def <= 526));

        if (!is_knife_entity)
            continue;


        const int selected_knife = GET_VAR(int, SKINS_PATH(m_knife_selected));

        if (selected_knife < 0 || selected_knife >= static_cast<int>(g_skins->knives_items.size()) ||
            selected_knife >= static_cast<int>(knives.indexes.size()) ||
            selected_knife >= static_cast<int>(knives.models.size()))
            continue;

        const auto& selected_knife_item = g_skins->knives_items[selected_knife];
        const auto& knives_settings = GET_VAR_VEC(c_vars::skins_t::skin_settings_t, SKINS_PATH(m_knives_settings));

        auto it = std::find_if(knives_settings.begin(), knives_settings.end(),
            [&selected_knife_item](const auto& setting) {
                return setting.m_item_definition_index == selected_knife_item.def_index;
            });

        if (selected_knife < 0 || selected_knife >= knives_settings.size())
            return;

        const auto& selected_setting = knives_settings[selected_knife];
        auto current_skin = selected_setting.m_paint_kit;

        const auto hash_it = k_knife_subclass_hashes.find(knives.indexes[selected_knife]);
        if (hash_it == k_knife_subclass_hashes.end())
            continue;

        const uint32_t target_subclass = hash_it->second;
        const int target_def_index = knives.indexes[selected_knife];

        static int last_knife_index = -1;
        bool knife_changed = (last_knife_index != selected_knife);

        c_weapon_base* weapon_base = (c_weapon_base*)weapon;

        bool weapon_needs_update = knife_changed
            || need_update_knifes
            || weapon->m_nFallbackPaintKit() != current_skin
            || weapon->m_flFallbackWear() != selected_setting.m_wear
            || weapon->m_nFallbackSeed() != selected_setting.m_seed
            || item->m_iItemDefinitionIndex() != static_cast<uint16_t>(target_def_index)
            || weapon_base->m_nSubclassID() != target_subclass;

        if (!weapon_needs_update)
            continue;

        item->m_iItemIDHigh() = 0xFFFFFFFFu;
        item->m_iItemIDLow() = 0u;
        item->m_bInitialized() = true;
        item->m_iItemDefinitionIndex() = static_cast<uint16_t>(target_def_index);

        weapon_base->m_nSubclassID() = target_subclass;
        weapon->set_model(knives.models[selected_knife]);

        weapon->m_nFallbackPaintKit() = current_skin;
        weapon->m_flFallbackWear() = selected_setting.m_wear;
        weapon->m_nFallbackSeed() = selected_setting.m_seed;
        weapon->m_nFallbackStatTrak() = -1;

       weapon_base->update_composite();
        weapon_base->update_weapon_data();
        weapon_base->update_composite_sec();
        weapon_base->update_subclass();
        weapon_base->update_model();
        weapon_base->regen_weapon_skin();

        const uint64_t mesh_mask = (target_def_index == 523) ? 2ull : 1ull;
        weapon->m_pGameSceneNode()->set_mesh_group_mask(mesh_mask);
        view_model->m_pGameSceneNode()->set_mesh_group_mask(mesh_mask);


        need_update_knifes = false;
        last_knife_index = selected_knife;
    }
}

void c_skins::weapons()
{
    if (!g_ctx->m_local_pawn || g_ctx->m_local_pawn->m_iHealth() <= 0)
        return;

    auto weapon_service = g_ctx->m_local_pawn->m_pWeaponServices();
    if (!weapon_service)
        return;


    c_base_view_model* view_model = g_ctx->m_local_pawn->get_view_model_brutforce();
    if (!view_model)
        return;

    c_network_utl_vector<c_base_handle>& my_weapons = weapon_service->m_hMyWeapons();

    for (auto i = 0; i < my_weapons.size; i++)
    {
        c_econ_entity* weapon = g_interfaces->m_entity_system->get_base_entity(my_weapons.elements[i].get_entry_index()).as<c_econ_entity*>();
        if (!weapon)
            continue;

        c_attribute_container* attribute_manager = weapon->m_AttributeManager();
        if (!attribute_manager)
            continue;

        c_econ_item_view* item = attribute_manager->m_Item();
        if (!item)
            continue;

        auto item_schema = g_interfaces->m_source2_client->get_econ_item_system()->get_econ_item_schema();
        if (!item_schema)
            continue;

        int item_definition_index = item->m_iItemDefinitionIndex();

        int current_weapon = get_skin_config(item_definition_index);
        if (current_weapon == CONFIG_UNKNOWN)
            continue;

     

        const auto& skins_settings = GET_VAR_VEC(c_vars::skins_t::skin_settings_t, SKINS_PATH(m_skin_settings));

        auto& current_skin = skins_settings[current_weapon];

        bool wants_update = weapon->m_nFallbackPaintKit() != current_skin.m_paint_kit
            || weapon->m_flFallbackWear() != current_skin.m_wear
            || weapon->m_nFallbackSeed() != current_skin.m_seed;

        if (current_skin.m_paint_kit > 0) {
            auto paint_kit = item_schema->get_paint_kits().find_by_key(current_skin.m_paint_kit);
            bool use_old_model = paint_kit.has_value() && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(paint_kit.value()) + 0xAE);

            uint64_t mesh_mask = 1 + static_cast<uint64_t>(use_old_model);


            c_weapon_base* weapon_base = (c_weapon_base*)weapon;
            weapon_base->m_nSubclassID() = c_utl_string_token(std::to_string(item->m_iItemDefinitionIndex()).c_str()).GetHashCode1();
            weapon_base->update_subclass();

            weapon->m_nFallbackPaintKit() = current_skin.m_paint_kit;
            weapon->m_flFallbackWear() = current_skin.m_wear;
            weapon->m_nFallbackSeed() = current_skin.m_seed;

            item->m_iItemIDHigh() = -1;

            weapon->m_pGameSceneNode()->set_mesh_group_mask(mesh_mask);
            view_model->m_pGameSceneNode()->set_mesh_group_mask(mesh_mask);
        }

        if (wants_update) {
            c_weapon_base* weapon_base = g_ctx->m_active_weapon;
         
       weapon_base->update_composite();

            weapon_base->update_weapon_data();
            weapon_base->update_composite_sec();
            weapon_base->update_subclass();

            weapon_base->update_model();
            weapon_base->regen_weapon_skin();



         
        }
    }
}

void c_skins::glove_changer()
{
    if (!g_ctx->m_local_pawn || g_ctx->m_local_pawn->m_iHealth() <= 0)
        return;

    const int selected_glove = GET_VAR(int, SKINS_PATH(m_glove_selected));
    if (selected_glove < 0 || selected_glove >= static_cast<int>(g_skins->glove_items.size()) ||
        selected_glove >= static_cast<int>(gloves.indexes.size()))
        return;

    const auto& gloves_settings = GET_VAR_VEC(c_vars::skins_t::skin_settings_t, SKINS_PATH(m_glove_settings));
    if (selected_glove >= gloves_settings.size())
        return;

    const auto& selected_setting = gloves_settings[selected_glove];
    auto current_skin = selected_setting.m_paint_kit;

    if (current_skin <= 0)
        return;

    c_econ_item_schema* econ_item_schema = g_interfaces->m_source2_client->get_econ_item_system()->get_econ_item_schema();
    if (!econ_item_schema)
        return;

    c_econ_item_view* item = &g_ctx->m_local_pawn->m_EconGloves();
    if (!item)
        return;

    int current_model = gloves.indexes[selected_glove];


    static int last_skin = -1;
    static int last_seed = -1;
    static float last_wear = -1.f;
    static int last_agent_ct = -1;
    static int last_agent_t = -1;
    static float last_spawn_time = -1.f;

    static int s_clear_frames = 0;
    static int s_update_frames = 0;
    static int last_glove = -1;

    const float spawn_now = g_ctx->m_local_pawn->m_flLastSpawnTimeIndex();
    const bool changed = (selected_glove != last_glove) || (current_skin != last_skin)
        || (selected_setting.m_seed != last_seed) || (selected_setting.m_wear != last_wear)
        || (spawn_now != last_spawn_time) || need_update_gloves;
    const bool engine_reset = (item->m_iItemDefinitionIndex() != current_model) || !item->m_bInitialized();

    if (changed) {
        s_clear_frames = 2;
        s_update_frames = 6;
    }
    else if (engine_reset) {
        s_update_frames = 6;
    }
    need_update_gloves = false;

    if (s_clear_frames > 0) {
        attr_remove(item);
        item->m_iItemDefinitionIndex() = 0;
        item->m_bInitialized() = false;
        g_ctx->m_local_pawn->m_bNeedToReApplyGloves() = true;
        set_glove_bodygroup(g_ctx->m_local_pawn);
        s_clear_frames--;
        last_glove = selected_glove;
        last_skin = current_skin;
        last_seed = selected_setting.m_seed;
        last_wear = selected_setting.m_wear;
        last_spawn_time = spawn_now;
        return;
    }

    if (s_update_frames <= 0)
        return;

    if (!g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;

    item->m_iItemDefinitionIndex() = current_model;
    item->m_iEntityQuality() = 3;

    using fn_create_new_paint_kit = c_paint_kit * (__fastcall*)(c_econ_item_view*);
    static fn_create_new_paint_kit create_new_paint_kit = g_modules->m_client.find(("48 89 5C 24 ?? 56 48 83 EC 20 48 8B 01 FF 50")).as< fn_create_new_paint_kit >();
    if (create_new_paint_kit) {
        if (auto* paint_kit = create_new_paint_kit(item)) {
            auto current_paint_kit = econ_item_schema->get_paint_kits().find_by_key(current_skin);
            if (current_paint_kit.has_value())
                *reinterpret_cast<const char**>(reinterpret_cast<uintptr_t>(paint_kit) + 0x8) = current_paint_kit.value()->paint_kit_name();
        }
    }

    attr_remove(item);
    attr_create(item, current_skin, selected_setting.m_wear, selected_setting.m_seed);

    item->m_iItemIDHigh() = 0xFFFFFFFFu;
    item->m_bDisallowSOC() = false;
    item->m_bRestoreCustomMaterialAfterPrecache() = true;
    if (g_ctx->m_local_controller)
        item->m_iAccountID() = static_cast<uint32_t>(g_ctx->m_local_controller->m_steamID() & 0xFFFFFFFF);
    item->m_bInitialized() = true;
    set_glove_bodygroup(g_ctx->m_local_pawn);
    g_ctx->m_local_pawn->m_bNeedToReApplyGloves() = true;

    last_glove = selected_glove;
    last_skin = current_skin;
    last_seed = selected_setting.m_seed;
    last_wear = selected_setting.m_wear;
    last_spawn_time = spawn_now;

    need_update_gloves = false;
    s_update_frames--;
}

void c_skins::update() {
    if (!g_interfaces->m_engine || !g_interfaces->m_engine->is_connected() || !g_interfaces->m_engine->in_game())
        return;
        
    bool is_alive = g_ctx->m_local_pawn && g_ctx->m_local_pawn->is_alive();
    int current_team = g_ctx->m_local_pawn ? g_ctx->m_local_pawn->m_iTeamNum() : 0;
    
    if (m_waiting_for_round_start_update) {
        need_update_knifes = true;
        need_update_gloves = true;
        m_waiting_for_round_start_update = false;
    }
    
    if (!m_waiting_for_round_start_update) {
        if (!m_was_alive && is_alive) {
            need_update_knifes = true;
            need_update_gloves = true;
        }
        
        if (m_last_team != current_team && current_team != 0) {
            need_update_knifes = true;
            need_update_gloves = true;
        }
    }
    
    m_was_alive = is_alive;
    m_last_team = current_team;
}

void c_skins::on_round_start() {
    m_waiting_for_round_start_update = true;
}
