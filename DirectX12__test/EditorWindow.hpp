/*! ************************************************************
 * \file   EditorWindow.hpp
 * \brief  Engineに使うWindowの作成
 *
 * 作成者 keeep
 * 作成日 2026/5/22
 * 更新履歴	5.22 作成
 *			5.23 Entityフィルターの作成
 *			5.28 Prototypeに向けてリファクタリング
 *			5.29 レイアウトの変更
 * *********************************************************************/
#pragma once

#include "SceneManager.hpp"
#include "World.hpp"
#include "DirectX.hpp"
#include "imguiinit.hpp"
#include "RenderTexture.hpp"
#include "Components.hpp"
#include "IconLibrary.hpp"

class EditorWindow
{
public:
	EditorWindow(_In_ DirectXApp& app);

	/// @brief 描画処理
	/// @param sceneManager 描画に必要なシーンマネージャーの参照 
	void Draw(_In_ SceneManager& sceneManager);

	inline RenderTexture* GetGameRenderTexture() const { return m_GameRenderTexture.get(); }

	inline RenderTexture* GetEditorRenderTexture() const { return m_EditorRenderTexture.get(); }

	ImVec2 GetViewportSize() const { return m_ViewportSize; }
private:
	/// @brief シーンの情報を描画する
	/// @param scene シーンの描法
	void DrawSceneInfo(_In_ Scene& scene);

	/// @brief エンティティリストを描画する
	/// @param world entityの情報を持つWorldクラスの参照
	void DrawEntityList(_In_ World& world);

	/// @brief インスペクターの描画
	/// @param world entityの情報を持つWorldクラスの参照
	void DrawInspector(
		_In_ World& world,
		_In_ Scene* scene);

	/// @brief プレハブパネルの描画
	/// @param world prefabの情報を持つWorldクラスの参照
	void DrawPrefabPanel(
		_In_ Scene& scene,
		_In_ World& world);

	/// @brief Asset一覧の描画
	void DrawAssetPanel();

	/// @brief メモリ使用量の描画
	void DrawMemoryPanel();

	/// @brief Scene保存 / 読み込み
	/// @param sceneManager シーンマネージャーの参照
	void DrawScenePanel(_In_ SceneManager& sceneManager);

	Entity m_SelectedEntity = INVALID_ENTITY;
	std::string m_SelectedPrefab;
	std::string m_SelectedAsset;
	std::string m_CurrentAssetDir = "Assets";
	float m_AssetCellSize = 72.0f;

	std::array<char, 64> m_EntityFilyer{};
	std::array<char, 64> m_SceneRegisterName{};
	std::array<char, 268> m_SceneRegisterPath{};
	std::array<char, 260> m_SceneNameInput{};
	float3 m_PrefabPosition{ 0.0f,0.0f,0.0f };

	DirectXApp& m_App;

	// ============================================//
	//		Unreal Engine スタイルのパネル表示フラグ	   //
	// ============================================//
	bool m_ShowOutliner = true;		// 左: アウトライナー（エンティティ一覧）
	bool m_ShowViewport = true;		// 中央: ビューポート
	bool m_ShowProperties = true;	// 右: プロパティパネル（インスペクタ）
	bool m_ShowDetails = true;		// 下: 詳細パネル
	bool m_ShowMemory = true;		// メモリ使用量表示
	bool m_DockLayout = false;

	// ビューポート情報
	ImVec2 m_ViewportSize{ 0.0f, 0.0f };
	ImVec2 m_ViewportPos{ 0.0f, 0.0f };

	// ゲーム画面用のレンダーテクスチャ
	std::unique_ptr<RenderTexture> m_GameRenderTexture;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GameRenderTextureHandle{};
	bool m_GameTextureHandleValid = false;

	// エディタ用のレンダーテクスチャ
	std::unique_ptr<RenderTexture> m_EditorRenderTexture;
	D3D12_CPU_DESCRIPTOR_HANDLE m_EditorRenderTextureHandle{};
	bool m_EditorTextureHandleValid = false;

	bool m_ShowColliderDebug = false;

	/// @brief 当たり判定描画
	/// @param collider colliderコンポーネント
	/// @param transform transformコンポーネント
	void DrawColliderDebug(
		_In_ const ColliderComponent& collider,
		_In_ const TransformComponent& transform
		);

	struct DebugLine
	{
		float3 start;
		float3 end;
		float4 color;
	};
	std::vector<DebugLine> m_DebugLines;

	bool m_ResizeGameRenderTexture = false;

	void SpawnModelFromFile(World& world,
		_In_ const std::string& modelpath,
		_In_ const float3& pos);
};