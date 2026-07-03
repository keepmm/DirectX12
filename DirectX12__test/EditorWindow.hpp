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

	void ReleaseRenderTextures();
private:
	/// @brief シーンの情報を描画する
	/// @param scene シーンの描法
	void DrawSceneInfo(_In_ Scene& scene);

	/// @brief エンティティリストを描画する
	/// @param world entityの情報を持つWorldクラスの参照
	void DrawEntityList(_In_ World& world);

	/// @brief 
	/// @param world 
	/// @param entity 
	void DrawEntityNode(_In_ World& world, _In_ Entity entity);

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
	void DrawAssetPanel(_In_ SceneManager& sceneManager);

	/// @brief メモリ使用量の描画
	void DrawMemoryPanel();

	/// @brief コンソールウィンドウの描画
	void DrawConsole();

	/// @brief プレイ/停止ボタンの描画
	void DrawPlayControl(_In_ Scene* activeScene);

	/// @brief Scene保存 / 読み込み
	/// @param sceneManager シーンマネージャーの参照
	void DrawScenePanel(_In_ SceneManager& sceneManager);

	/// @brief canvasの検索または作成
	/// @param world worldの参照
	/// @return 既に存在する場合はIDを返す、存在しない場合は新規作成してIDを返す
	Entity EnsureCanvas(World& world);

	/// @brief imageの作成(Entity)
	/// @param world worldの参照
	/// @return 作成したEntityのID
	Entity CreateImage(_In_ World& world);

	/// @brief Textの作成(Entity)
	/// @param world worldの参照
	/// @return 作成したEntityのID
	Entity CreateText(_In_ World& world);

	/// @brief InspectorにAddComponentのポップアップを表示する
	/// @param world worldの参照
	/// @param entity entityのID
	void DrawAddComponentPopup(_In_ World& world, _In_ Entity entity);

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
	bool m_ShowConsole = true;		// コンソール表示
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

	int m_GizmoOperation = 7;

	std::string m_PlaySnap = "";

	bool m_ShowCreateScriptPopup = false;
	char m_NewScriptName[64] = "NewScript";
	void CreateScriptFile(const std::string& die, const std::string& name);

	void OpenInEditor(const std::string& path);

	void AddToProject(_In_ const std::string cppRel, _In_ const std::string hppRel);
	void CreateFolder(_In_ const std::string& dir);

	char m_ScriptSerachBuffer[128] = {};
	bool m_FocusScriptSearch = false;

	char m_AddCompSearchBuffer[128] = {};
	bool m_FocusAddCompSearch = false;
};