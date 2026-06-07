///*****************************************************************//**
// * \file   SampleScene.hpp
// * \brief  サンプルシーン
// * 
// * 作成者 keeeep
// * 作成日 2026/5/8
// * 更新履歴	5.8 作成
// *			5.8 ボーンの表示を追加
// *			5.15 player追加
// * *********************************************************************/
//#pragma once
//
//#include "Scene.hpp"
//#include "Defines.hpp"
//#include "World.hpp"
//#include "Systems.hpp"
//#include "DebugLineRenderer.hpp"
//#include "ModelData.hpp"
//
//class Player;
//class Camera;
//
//class SampleScene :
//	public Scene
//{
//public:
//	SampleScene(
//		_In_ const ComPtr<ID3D12Device>& device,
//		_In_ const DirectXApp::PipelineStateTable pipelinestates,
//		_In_ const ComPtr<ID3D12PipelineState>& wirePso,
//		_In_ const ComPtr<ID3D12PipelineState>& linePso);
//	~SampleScene() final;
//
//	// ---- ライフサイクル ---- //
//
//	void OnLoad() final;
//	void OnStart() final;
//	void OnActivate() final;
//	void OnDeactivate() final;
//	void OnPause() final;
//	void OnResume() final;
//	void OnUnload() final;
//
//	void Draw(_In_ const RenderContext& renderContext) final;
//
//
//	// ---- 更新処理 ---- //
//	void Update(_In_ float deltatime) final;
//	void FixedUpdate(_In_ float fixedDeltatime) final;
//	void LateUpdate(_In_ float deltatime) final;
//
//	/// @brief デバッグウィンドウの表示(未実装)
//	void DebugWindow();
//
//	/// @brief グリッドの表示
//	void DrawGrid();
//private:
//	void DrawSkeleton(
//		_In_ const RenderContext& renderContext);
//
//	void UpdateLighting(_In_ float deltatime);
//	void UpdatePlayer(_In_ float deltatime);
//
//	ComPtr<ID3D12Device> m_Device;
//	DirectXApp::PipelineStateTable m_PipelineStates;
//	ComPtr<ID3D12PipelineState> m_WirePso;
//	ComPtr<ID3D12PipelineState> m_LinePso;
//
//	std::unique_ptr<Camera> m_Camera;
//	std::unique_ptr<Player> m_Player;
//
//	World m_World;
//	Entity m_CubeEntity = INVALID_ENTITY;
//
//	SpinSystem m_SpinSystem;
//	RenderSystem m_RenderSystem;
//
//	Skeleton m_Skeleton;
//	DebugLineRenderer m_DebugLineRenderer;
//
//	// ライト情報
//	float m_LightAngle = 0.0f;
//	float m_AmbientAngle = 0.0f;
//	float3 m_LightDir{ 0.0f, -1.0f, 0.0f };
//	float4 m_LightColor{ 1.0f, 1.0f, 1.0f,1.0f };
//	float4 m_AmbientColor{ 0.2f, 0.2f, 0.2f, 1.0f };
//
//	bool m_IsInitialized = false;
//};