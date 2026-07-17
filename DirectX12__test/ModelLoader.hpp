/*****************************************************************//**
 * \file   ModelLoader.hpp
 * \brief  assimp使いたい
 * 
 * 作成者 keeeep
 * 作成日 2026/5/4
 * 更新履歴	5.04 作成
 * 　　　　	6.28 非同期ロード実装
 * *********************************************************************/
#pragma once

#include "ModelData.hpp"
#include "World.hpp"

class Scene;
class Mesh;

class ModelLoader
{
public:
	static ModelLoadResult LoadFromFile(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const std::string& filepath,
		float scale = 1.0f);

	static ModelCpuData ParseFile(
		_In_ const std::string& filepath,
		_In_ float scale= 1.0f
	);

	static ModelLoadResult upload(
		_In_ const ModelCpuData& cpu
	);

	static std::vector<AnimationClip> LoadAnimationsOnly(_In_ const std::string& filepath);

	static AnimationClip LoadVMDClip(const std::string& path,const Skeleton& skeleton);

	static CameraClip LoadVMDCameraClip(_In_ const std::string& path);

	static void PopulateModelEntity(
		World& world, std::uint32_t entity,
		const std::string& modelpath, Scene* scene,
		int  restoreClip = -1,
		bool restorePlaying = false,
		const std::vector<std::string>& extraVmds = {},
		bool applyDefaultTransformScale = true);
};