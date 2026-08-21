#pragma once

#include <context.h>

class c_view_setup {
public:
	PAD(0x498);
	float m_world_fov; // 0x498
	float m_viewmodel_fov; // 0x49C
	vec3_t m_origin; // 0x4A0
	PAD(0xC);
	qangle_t m_view_angle; // 0x4B8
	PAD(0x10);
	float m_aspect_ratio; // 0x4D4
	bool m_ortho; // 0x4D8
	PAD(0x3);
	float flOrthoLeft; // 0x4DC
	float flOrthoTop; // 0x4E0
	float flOrthoRight; // 0x4E4
	float flOrthoBottom; // 0x4E8
	float flOrthoZoom; // 0x4EC
	PAD(0x60);
	BYTE pad5; // 0x550
	unsigned char m_some_flags; // 0x551
};


struct c_view_render {
	PAD( 8 );
	class c_view_setup m_view_setup;
	PAD( 8 );
};