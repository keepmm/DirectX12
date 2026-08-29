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
#include "BeamRenderer.hpp"
#include "FireworkSystem.hpp"
#include "SePlayer.hpp"

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

	void SetSceneName(_In_ const std::string& name) { m_SceneName = name; }

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
	void EditorUpdate(float dt);

	/// @brief スカイボックスを差し替えて即反映
	void SetSkybox(_In_ const std::string& path)
	{
		SetSkyboxPath(path);
		ApplySkybox();
	}

	/// @brief スクリプトから花火を打ち上げるための橋渡し
	void LaunchFirework(const float3& pos, int shape, const float3& color, const char* text)
	{
		m_FireworkSystem.Launch(pos,
			static_cast<FireworkSystem::Shape>(shape), color, text ? text : "");
	}
	static RuntimeScene* Current() { return s_Current; }
private:
	void DrawGizmos(const RenderContext& renderContext);

	void DrawGrid();

	void DrawLight();

	void DrawColliders();

	void DrawLaserBeams(const RenderContext& renderContext, ID3D12PipelineState* psoOverride = nullptr,bool emitFirework = false);

	/// @brief 現在のスカイボックスパスでマテリアルを(再)生成
	void ApplySkybox();


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
	ScriptSystem m_ScriptSystem;
	SpriteRenderSystem m_SpriteRenderSystem;
	CanvasRenderSystem m_CanvasRenderSystem;
	AudioSystem m_AudioSystem;
	TransformSystem m_TransformSystem;
	ShadowSystem m_ShadowSystem;
	AnimatorSystem m_AnimatorSystem;
	ParticleSystem m_ParticleSystem;
	BeamRenderer m_BeamRenderer;
	CameraAnimationSystem m_CameraAnimationSystem;
	MusicSyncSystem m_MusicSyncSystem;
	SePlayer m_SePlayer;

	BeamRenderer m_FireworkBeamRenderer;
	FireworkSystem m_FireworkSystem;

	bool m_ScriptSystemStarted = false;

	DebugLineRenderer m_DebugLineRenderer;
	std::vector<DebugLine> m_DebugLines;

	Mesh m_IconQuad;
	Mesh m_SpriteQuad;
	Mesh m_UIQuad;
	Mesh m_SkyboxCube;
	std::shared_ptr<Material> m_LightIcon;
	std::shared_ptr<Material> m_CameraIcon;
	std::shared_ptr<Material> m_SkyBox;
	bool n_IconReady;

	bool m_Initialized = false;

	std::string m_SceneName = "";
	static inline RuntimeScene* s_Current = nullptr;
};

