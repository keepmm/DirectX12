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

class Scene;

class SceneSerializer
{
public:
	static bool Save(
		_In_ Scene& scene,
		_In_ const std::string& filePath);
	static bool Load(
		_In_ Scene& scene,
		_In_ const std::string& filePath);
private:
};

