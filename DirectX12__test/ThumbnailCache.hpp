/*!*************************************************************
 * \file   ThumbnailCache.hpp
 * \brief  アセットブラウザ用のサムネイル(モデル / マテリアル / 画像)
 *
 * 作成者 keeep
 * 作成日 2026/9/14
 * 更新履歴	9.14 作成
 *
 * \note 重いので 1 枚ずつ作り、Library/Thumbnails に PNG で保存する。
 *       ファイル名に更新時刻を入れるので、アセットを変更すると作り直される
 * *********************************************************************/
#pragma once

#include <deque>
#include <future>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "Defines.hpp"
#include "imguiinit.hpp"
#include "RenderTexture.hpp"

class Material;
class Mesh;
struct ModelCpuData;

class ThumbnailCache
{
public:
	static ThumbnailCache& Get();

	/// @brief サムネイルを要求する(アセットブラウザがタイルを描くたびに呼ぶ)
	/// @param assetPath UTF-8 のパス
	/// @return 用意できていれば ImTextureID、まだなら 0(呼び出し側は通常のアイコンを出す)
	ImTextureID Request(_In_ const std::string& assetPath);

	/// @brief サムネイルを作れる拡張子か(小文字の ".fbx" など)
	static bool Supports(_In_ const std::string& extLower);

	/// @brief 読み込み完了の受け取り・読み戻しを進める(UI 構築の前に毎フレーム)
	void Update();

	/// @brief 描画待ちがあれば描く(MaterialPreview::RenderRequested と同じタイミング)
	void Render(_In_ ID3D12GraphicsCommandList* cmd, UINT frameIndex);

	void Release();

private:
	enum class Kind { Texture, Model, Material };
	enum class Stage { Idle, Loading, WaitRender, WaitCapture };

	struct Job
	{
		std::string assetPath;		// UTF-8
		std::wstring cachePath;		// Library/Thumbnails/xxx.png
		Kind kind = Kind::Texture;
	};

	ThumbnailCache() = default;

	/// @brief キャッシュの PNG のパス。GUID と更新時刻から作る(取れなければ空)
	std::wstring CachePathOf(_In_ const std::string& assetPath) const;

	void StartNext();
	void FinishModelLoad();
	void Capture();
	void Finish(bool ok);

	std::deque<Job> m_Queue;
	std::unordered_set<std::wstring> m_Queued;	// 同じものを何度も積まない
	std::unordered_set<std::wstring> m_Failed;	// 作れなかったもの(毎フレーム再挑戦しない)

	Job   m_Current;
	Stage m_Stage = Stage::Idle;
	int   m_CaptureWait = 0;

	// ---- 作成中のデータ ---- //
	std::future<bool> m_TextureTask;					// 画像は CPU だけで PNG まで作る
	std::future<std::shared_ptr<ModelCpuData>> m_ModelTask;
	std::shared_ptr<Mesh> m_Mesh;
	std::vector<std::shared_ptr<Material>> m_Materials;
	std::shared_ptr<Material> m_Material;				// .mat 用

	float3 m_Center{ 0.0f, 0.0f, 0.0f };				// カメラが見る中心
	float  m_Radius = 1.0f;								// 収める半径

	RenderTexture m_Target;
};
