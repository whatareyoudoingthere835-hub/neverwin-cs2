#pragma once

#include "../datatypes/utl_map.h"

enum econ_item_definition_offsets : uintptr_t
{
	MODEL_NAME = 0x148,
	STICKERS_SUPPORTED_COUNT = 0x168,
	SIMPLE_WEAPON_NAME = 0x260,
	LOADOUT_SLOT = 0x338,
};

enum econ_item_system : uintptr_t
{
	ECON_ITEM_SCHEMA = 0x8,
};

class c_econ_item_definition
{
public:
	char m_pad_001[ 0x8 ];  // vtable
	void* m_kv_item; // 0x8
	uint16_t m_definition_index; // 0x10
	char m_pad_002[ 0x1e ];
	bool m_enabled; // 0x30
	char m_pad_003[ 0xf ];
	uint8_t m_min_itelevel; // 0x40
	uint8_t m_max_itelevel; // 0x41
	uint8_t m_iterarity; // 0x42
	uint8_t m_itequality; // 0x43
	uint8_t m_forced_itequality; // 0x44
	uint8_t m_default_drop_itequality; // 0x45
	uint8_t m_default_drop_quantity; // 0x46
	char m_pad_004[ 0x19 ];
	uint8_t m_popularity_seed; // 0x60
	char m_pad_005[ 0x7 ];
	void* m_portraits_kv; // 0x68
	char* m_item_base_name; // 0x70
	bool m_proper_name; // 0x78
	char m_pad_006[ 0x7 ];
	char* m_item_type_name; // 0x80
	char m_pad_007[ 0x8 ];
	char* m_item_desc; // 0x90
	uint32_t m_expiration_time_stamp; // 0x98
	uint32_t m_creation_time_stamp; // 0x9c
	char* m_inventory_model; // 0xa0
	char* m_inventory_image; // 0xa8
	char m_pad_008[ 0x18 ];
	int m_inventory_image_position[ 2 ]; // 0xc8
	int m_inventory_image_size[ 2 ]; // 0xd0
	char* m_base_display_model; // 0xd8
	bool m_load_on_demand; // 0xe0
	char m_pad_009[ 0x1 ];
	bool m_hide_body_groups_deployed_only; // 0xe2
	char m_pad_010[ 0x5 ];
	char* m_world_display_model; // 0xe8
	char* m_holstered_model; // 0xf0
	char* m_world_extra_wearable_model; // 0xf8
	uint32_t m_sticker_slots; // 0x100
	char m_pad_011[ 0x4 ];
	char* m_icon_default_image; // 0x108
	bool m_attach_to_hands; // 0x110
	bool m_attach_to_hands_vonly; // 0x111
	bool m_flip_view_model; // 0x112
	bool m_act_as_wearable; // 0x113
	char m_pad_012[ 0x24 ];
	uint32_t m_item_type; // 0x138
	char m_pad_013[ 0x4 ];
	char* m_brass_model_override; // 0x140
	char* m_zooin_sound_path; // 0x148
	char* m_zooout_sound_path; // 0x150
	char m_pad_014[ 0x18 ];
	uint32_t m_sound_material_id; // 0x170
	bool m_disable_style_selection; // 0x174
	char m_pad_015[ 0x13 ];
	char* m_particle_file; // 0x188
	char* m_particle_snapshot_file; // 0x190
	char m_pad_016[ 0x40 ];
	char* m_item_classname; // 0x1d8
	char* m_item_log_classname; // 0x1e0
	char* m_item_icon_classname; // 0x1e8
	char* m_definition_name; // 0x1f0
	bool m_hidden; // 0x1f8
	bool m_should_show_in_armory; // 0x1f9
	bool m_base_item; // 0x1fa
	bool m_flexible_loadout_default; // 0x1fb
	bool m_imported; // 0x1fc
	bool m_one_per_account_cdkey; // 0x1fd
	char m_pad_017[ 0xa ];
	char* m_armory_desc; // 0x208
	char m_pad_018[ 0x8 ];
	char* m_armory_remap; // 0x218
	char* m_store_remap; // 0x220
	char* m_class_token; // 0x228
	char* m_slot_token; // 0x230
	uint32_t m_drop_type; // 0x238
	char m_pad_019[ 0x4 ];
	char* m_holiday_restriction; // 0x240
	uint32_t m_sub_type; // 0x248
	char m_pad_020[ 0xc ];
	uint32_t m_equip_region_mask; // 0x258
	uint32_t m_equip_region_conflict_mask; // 0x25c
	char m_pad_021[ 0x50 ];
	bool m_public_item; // 0x2b0
	bool m_ignore_in_collection_view; // 0x2b1
	char m_pad_022[ 0x36 ];
	int m_loadout_slot; // 0x2e8
	char m_pad_023[ 0x94 ];

	auto get_item_definition_index() {
		return *reinterpret_cast<uint16_t*>((uintptr_t)(this) + 0x10);
	}

	bool is_weapon( );
	bool is_agent( );
	bool is_knife( bool exclude_default );
	bool is_glove( bool exclude_default );
	bool is_weapon_case( );
	bool is_key( );

	const char* get_model_name( );
	const char* get_simple_weapon_name( );

	int get_stickers_supported_count( );
	int get_loadout_slot( );
};

struct c_alternate_icon_data
{
	const char* m_simple_name;
	const char* m_large_simple_name;
	char m_pad_001[ 0x10 ];
};


class c_paint_kit
{
public:
	char pad_0x0000[0xE0]; //0x0000

	std::uint64_t paint_kit_id()
	{
		return *reinterpret_cast<std::uint64_t*>((uintptr_t)(this));
	}

	const char* paint_kit_name()
	{
		return *reinterpret_cast<const char**>((uintptr_t)(this) + 0x8);
	}

	const char* paint_kit_description_string()
	{
		return *reinterpret_cast<const char**>((uintptr_t)(this) + 0x10);
	}

	const char* paint_kit_description_tag()
	{
		return *reinterpret_cast<const char**>((uintptr_t)(this) + 0x18);
	}

	uint32_t paint_kit_rarity()
	{
		return *reinterpret_cast<uint32_t*>((uintptr_t)(this) + 0x44);
	}

	bool uses_old_model()
	{
		return *reinterpret_cast<bool*>((uintptr_t)(this) + 0xAE);
	}
};

class c_sticker_kit
{
public:
	int m_id;
	int m_rarity;
	const char* m_name;
	const char* m_description;
	const char* m_item_name;
	const char* m_material_path;
	const char* m_material_path_no_drips;
	const char* m_inventory_image;
	int m_tournament_id;
	int m_tournament_team_id;
	int m_tournament_player_id;
	bool m_material_path_is_absolute;
	bool m_cannot_trade;
	char m_pad_001[ 0x2 ];
	float m_rotate_start;
	float m_rotate_end;
	float m_scale_min;
	float m_scale_max;
	float m_wear_min;
	float m_wear_max;
	const char* m_icon_url_small;
	const char* m_icon_url_large;
	void* m_kv_item;
};

class c_econ_item_schema {
public:
	//std::byte pad_001[0x130];
	//c_utl_map<int, c_econ_item_definition*> m_sorted_item_definition_map;
	//std::byte pad_002[0x128];
	//c_utl_map<uint64_t, c_alternate_icon_data> m_alternate_icons_map;
	//std::byte pad_003[0x50];
	//c_utl_map<int, c_paint_kit*> m_paint_kits;
	//c_utl_map<int, c_sticker_kit*> m_sticker_kits;

	auto& get_stickers() {
		return *reinterpret_cast<c_utl_map<int, c_sticker_kit*>*>((uintptr_t)(this) + 0x318);
	}

	auto& get_sorted_item_definition_map()
	{
		return *reinterpret_cast<c_utl_map<int, c_econ_item_definition*>*>(
			(uintptr_t)(this) + 0x128);
	}

	auto& get_alternate_icons_map()
	{
		return *reinterpret_cast<c_utl_map<uint64_t, c_alternate_icon_data>*>(
			(uintptr_t)(this) + 0x268);
	}

	auto& get_paint_kits()
	{
		return *reinterpret_cast<c_utl_map<int, c_paint_kit*>*>((uintptr_t)(this) +
			0x2F0);
	}
};


class c_econ_item_system
{
public:
	c_econ_item_schema* get_econ_item_schema( );
};