/*! ************************************************************
 * \file   TextureLoader.hpp
 * \brief  画像ファイル / メモリから DirectXTex へ読み込む
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 Material から分離して作成
 *
 * \note D3D12 リソースは作らない。「パス → ScratchImage」までが責務
 * *********************************************************************/
#pragma once

#include <string>
#include <cstdint>
#include <DirectXTex.h>

namespace TextureLoader
{
	/// @brief テクスチャの読み込み(拡張子に応じて .hdr / .dds / .tga / WIC を自動判別)
	/// @param filePath ファイルのパス(Assets 相対も可。ResolveAssetPath を通す)
	/// @param meta 読み込んだ metadata
	/// @param img 読み込んだ ScratchImage
	/// @return 成功時は S_OK、失敗時はエラーコード
	HRESULT LoadAny(
		_In_ const std::wstring& filePath,
		_Out_ DirectX::TexMetadata& meta,
		_Out_ DirectX::ScratchImage& img);

	/// @brief メモリ上の画像を WIC で読み込む
	HRESULT LoadFromMemory(
		_In_ const std::uint8_t* data,
		size_t size,
		_Out_ DirectX::TexMetadata& meta,
		_Out_ DirectX::ScratchImage& img);

	/// @brief WIC 限定で読み込む(トゥーンランプなど、拡張子判定が不要な用途)
	HRESULT LoadWic(
		_In_ const std::wstring& filePath,
		_Out_ DirectX::TexMetadata& meta,
		_Out_ DirectX::ScratchImage& img);
}
