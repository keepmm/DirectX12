/*! ************************************************************
 * \file   MaterialTextures.hpp
 * \brief  マテリアルが持つテクスチャ群と SRV ヒープの管理
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 Material から分離して作成
 *
 * \note 以前は diffuse / ramp / normal / metal / rough / emissive / occlusion の
 *       7 組が同じ形の4変数(texture / upload / footprint / pending)として
 *       Material に並んでいた。TextureSlot 1 つにまとめて配列で持つ
 * *********************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <DirectXTex.h>

#include "Defines.hpp"

/// @brief SRV ヒープ上の並び(シェーダーの t0..t9 と一致させること)
namespace TexSlot
{
	enum : UINT
	{
		Albedo      = 0,	// t0 ベースカラー
		Ramp        = 1,	// t1 トゥーンランプ
		Normal      = 2,	// t2 法線
		Metal       = 3,	// t3 メタリック
		Rough       = 4,	// t4 ラフネス
		Environment = 5,	// t5 環境マップ(APP 所有)
		ShadowMap   = 6,	// t6 シャドウマップ(APP 所有)
		Emissive    = 7,	// t7 エミッシブ
		Reflection  = 8,	// t8 平面反射RT(APP 所有)
		Occlusion   = 9,	// t9 AO
		Count       = 10
	};
}

/// @brief テクスチャ 1 枚分の GPU リソース一式
struct TextureSlot
{
	ComPtr<ID3D12Resource> texture;
	ComPtr<ID3D12Resource> upload;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	bool pending = false;	// アップロードバッファ → テクスチャのコピー待ち
	bool valid   = false;	// 有効なテクスチャが入っているか(シェーダーのフラグに使う)
};

struct MaterialCB;

class MaterialTextures
{
public:
	/// @brief SRV ヒープ(t0..t9)を確保し、全 slot を null descriptor で埋める
	/// @return 成功なら true。既に確保済みなら何もせず true
	bool EnsureHeap();

	ID3D12DescriptorHeap* Heap() const noexcept { return m_Heap.Get(); }

	/// @brief その slot に有効なテクスチャが入っているか
	bool Valid(UINT slot) const noexcept { return m_Slots[slot].valid; }

	/// @brief 2x2 の白テクスチャを Albedo に作る(ヒープもここで確保される)
	bool CreateWhite();

	/// @brief Albedo の 2x2 を単色で塗り替える(リソースは作り直さない)
	bool SetSolidColor(_In_ const COLOR& color);

	/// @brief 既定の 2 段階ランプ(暗→明)を Ramp に作る
	void CreateDefaultRamp();

	/// @brief RGBA8 の生データから slot のテクスチャを作る
	bool CreateFromRGBA(
		UINT slot, UINT width, UINT height, _In_ const std::uint8_t* rgba,
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

	/// @brief DirectXTex の画像から slot のテクスチャを作る
	/// @param srgb true なら metadata のフォーマットを sRGB へ読み替える
	bool UploadImage(
		UINT slot,
		_In_ const DirectX::Image* srcImage,
		_In_ const DirectX::TexMetadata& metadata,
		bool srgb = false);

	/// @brief 画像ファイルから slot のテクスチャを作る
	bool LoadFromFile(UINT slot, _In_ const std::wstring& path, bool srgb = false);

	/// @brief 他マテリアルの Albedo リソースを共有する(参照カウントのみ)
	void ShareAlbedo(_In_ const MaterialTextures& src);

	/// @brief pending の slot をコマンドリストへ流し込む
	void Flush(_In_ ID3D12GraphicsCommandList* commandList);

	/// @brief APP が持つ環境マップ / シャドウマップ / 反射RT を自ヒープへ張る
	/// @param reflectStrength 0 以下なら反射は張らない
	void BindGlobals(float reflectStrength);

	/// @brief テクスチャの有無を定数バッファのフラグへ書き込む
	void FillCB(_Out_ MaterialCB& out) const;

	/// @brief トゥーンランプが既定のものから差し替えられているか
	bool HasCustomRamp() const noexcept { return m_HasCustomRamp; }
	void MarkCustomRamp() noexcept { m_HasCustomRamp = true; }

private:
	/// @brief slot 番目に SRV を作る
	void CreateSrv(UINT slot, DXGI_FORMAT format, UINT mipLevels, _In_ ID3D12Resource* resource);

	/// @brief テクスチャ本体 + アップロードバッファを作り、footprint を得る
	bool CreateResources(
		_Inout_ TextureSlot& s,
		_In_ const D3D12_RESOURCE_DESC& texDesc,
		_Out_ UINT64& outRowSize);

	void BindEnvironmentIfNeeded();
	void BindShadowMapIfNeeded();
	void BindReflectionIfNeeded();

	ComPtr<ID3D12DescriptorHeap> m_Heap;
	std::array<TextureSlot, TexSlot::Count> m_Slots;

	bool m_HasCustomRamp = false;	// 既定ランプのままなら false

	// --- APP 所有リソースの張り替え検知 ---
	bool  m_EnvBound   = false;
	UINT  m_EnvGen     = UINT_MAX;	// 環境マップの差し替え検知
	float m_EnvMaxMip  = 0.0f;
	bool  m_ShadowBound = false;

	// 反射RTは作り直されることがあるので、張った相手を覚えて差し替えを検知する
	// 同じアドレスに作り直されても取りこぼさないよう世代でも見る
	ID3D12Resource* m_ReflectionBound = nullptr;
	UINT m_ReflectionGen = UINT_MAX;
};
