#pragma once

#include <sdk/entity/controller.h>

class c_prediction
{
public:
	char pad_0000[48]; //0x0000
	int32_t m_call_reason; //0x0030
	bool m_in_prediction; //0x0034
	bool m_engine_pause; //0x0035
	char pad_0036[14]; //0x0036
	int32_t m_snapshot_tick; //0x0044
	char pad_0048[8]; //0x0048
	bool m_supress_prediction; //0x0050
	char pad_0051[7]; //0x0051
	class c_prediction_data* m_prediction_data; //0x0058
	char pad_0060[32]; //0x0060
	bool m_in_current_tick; //0x0080
	bool m_first_prediction; //0x0081
	char pad_0082[78]; //0x0082
	c_cs_player_pawn* m_player_pawn; //0x00D0
	void* m_networked_schema_offsets; //0x00D8
	int32_t m_transmition_packet_count; //0x00E0
	int32_t m_max_game_transmition; //0x00E4
	void* m_game_translution; //0x00E8
	char pad_00F0[848]; //0x00F0
};