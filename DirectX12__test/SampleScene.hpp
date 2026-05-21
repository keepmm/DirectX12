/*****************************************************************//**
 * \file   SampleScene.hpp
 * \brief  サンプルシーン
 * 
 * 作成者 keeeep
 * 作成日 2026/5/8
 * 更新履歴	5.8 作成
 *			5.8 ボーンの表示を追加
 *			5.15 player追加
 * *********************************************************************/
#pragma once

#include "Scene.hpp"
#include "Defines.hpp"
#include "World.hpp"
#include "Systems.hpp"
#include "DebugLineRenderer.hpp"
#include "ModelData.hpp"

class Player;
class Camera;

class SampleScene :
	public Scene
{
public:
	SampleScene(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const DirectXApp::PipelineStateTable pipelinestates,
		_In_ const ComPtr<ID3D12PipelineState>& wirePso,
		_In_ const ComPtr<ID3D12PipelineState>& linePso);
	~SampleScene() final;

	void Update() final;
	void Draw(_In_ const RenderContext& renderContext) final;

	void DebugWindow();
private:
	void DrawSkeleton(
		_In_ const RenderContext& renderContext);

	std::unique_ptr<Camera> m_Camera;
	std::unique_ptr<Player> m_Player;

	World m_World;
	Entity m_CubeEntity = INVALID_ENTITY;

	SpinSystem m_SpinSystem;
	RenderSystem m_RenderSystem;

	Skeleton m_Skeleton;
	DebugLineRenderer m_DebugLineRenderer;
};