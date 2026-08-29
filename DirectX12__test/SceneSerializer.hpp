/*****************************************************************//**
 * \file   SceneSerializer.hpp
 * \brief  sceneをjsonを使って保存/読み込みをする
 * 
 * 作成者 keeeeep
 * 作成日 2026/5/21
 * 更新履歴	5.21 作成
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

	static std::string SaveToString(_In_ Scene& scene);
	static bool LoadFromString(_In_ Scene& scene, _In_ const std::string& data);
private:
};

