/*****************************************************************//**
 * \file   SceneSerializer.hpp
 * \brief  scene‚ğjson‚ğg‚Á‚Ä•Û‘¶/“Ç‚İ‚İ‚ğ‚·‚é
 * 
 * ì¬Ò keeeeep
 * ì¬“ú 2026/5/21
 * XV—š—ğ	5.21 ì¬
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "World.hpp"
#include "PhysicsWorld.hpp"

struct SceneData
{
	float3 pos{ 0.0f,0.0f,0.0f };
	float3 size{ 1.0f,1.0f,1.0f };
	float4 rot{ 0.0f,0.0f,0.0f,1.0f };
	float mass{ 1.0f };
	bool isStatic = false;
	bool isKinematic = false;
};

class SceneSerializer
{
public:
private:
};

