#pragma once
#include "Defines.hpp"

namespace GameAPI
{
	inline void (*g_LaunchFirework)(float, float, float, int,
		float, float, float, const char*) = nullptr;

	// FireworkSystem::Shape ‚Æ“¯‚¶•À‚Ñ
	enum FireworkShape { Peony = 0, Willow, Ring, Heart, Senrin, Text };

	inline void LaunchFirework(const float3& pos, FireworkShape shape,
		const float3& color, const char* text = "")
	{
		if (g_LaunchFirework)
			g_LaunchFirework(pos.x, pos.y, pos.z, (int)shape,
				color.x, color.y, color.z, text);
	}
}