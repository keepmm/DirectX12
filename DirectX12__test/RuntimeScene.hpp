/*****************************************************************//**
 * \file   RuntimeScene.hpp
 * \brief  動的にシーンを使うクラス
 * 
 * 作成者 keeeeep
 * 作成日 2026/5/26
 * 更新履歴 5.26 作成
 * *********************************************************************/
#pragma once

#include "Scene.hpp"
#include "Systems.hpp"
#include "DebugLineRenderer.hpp"

class Mesh;
class Material;

class RuntimeScene :
    public Scene
{
public:
    explicit RuntimeScene(
		_In_ std::string sceneFilePath,
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const ComPtr<ID3D12PipelineState>& linePso);
    ~RuntimeScene() = default;

    void OnLoad() final;
    void OnUnload() final;

	void Update(_In_ float deltatime) final;
	void FixedUpdate(_In_ float fixedDeltatime) final;
	void LateUpdate(_In_ float deltatime) final;

    void Draw(_In_ const RenderContext& renderContext) final;

	void SetSceneFilePath(_In_ const std::string& path) { m_SceneFilePath = path; }

	void AddDebugLine(
		_In_ const float3& start,
		_In_ const float3& end,
		_In_ const float4& color)
	{
		m_DebugLines.push_back({ start, end, color });
	}

	void ClearDebugLines()
	{
		m_DebugLines.clear();
	}
private:
	void DrawGizmos(const RenderContext& renderContext);

	void DrawGrid();

	void DrawLight();

	struct DebugLine
	{
		float3 start;
		float3 end;
		float4 color;
	};

	ComPtr<ID3D12Device> m_Device;
	ComPtr<ID3D12PipelineState> m_LinePso;

    std::string m_SceneFilePath;

	SpinSystem m_SpinSystem;
	RenderSystem m_RenderSystem;
	LightSystem m_LightSystem;
	FreeLookSystem m_FreeLookSystem;
	CameraSystem m_CameraSystem;

	DebugLineRenderer m_DebugLineRenderer;
	std::vector<DebugLine> m_DebugLines;

	Mesh m_IconQuad;
	std::shared_ptr<Material> m_LightIcon;
	std::shared_ptr<Material> m_CameraIcon;
	bool n_IconReady;

	bool m_Initialized = false;
};

