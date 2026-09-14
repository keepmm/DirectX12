/*! ************************************************************
 * \file   TextureLoader.cpp
 * \brief  画像ファイル / メモリから DirectXTex へ読み込む
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 Material から分離して作成
 * *********************************************************************/
#include "TextureLoader.hpp"

#include <algorithm>
#include <filesystem>

#include "Logger.hpp"
#include "Util.hpp"

namespace
{
	bool IExtEquals(std::wstring a, const wchar_t* b)
	{
		std::transform(a.begin(), a.end(), a.begin(), ::towlower);
		return a == b;
	}
}

HRESULT TextureLoader::LoadAny(
	const std::wstring& filePath,
	DirectX::TexMetadata& meta,
	DirectX::ScratchImage& img)
{
	const std::filesystem::path resolved = ResolveAssetPath(filePath);

	if (!std::filesystem::exists(resolved))
	{
		LOG->LogError("TextureLoader::LoadAny: " + resolved.string() + " not found");
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	}

	const std::wstring ext = resolved.extension().wstring();

	if (IExtEquals(ext, L".hdr"))
	{
		return DirectX::LoadFromHDRFile(resolved.c_str(), &meta, img);
	}
	if (IExtEquals(ext, L".dds"))
	{
		return DirectX::LoadFromDDSFile(resolved.c_str(), DirectX::DDS_FLAGS_NONE, &meta, img);
	}
	if (IExtEquals(ext, L".tga"))
	{
		return DirectX::LoadFromTGAFile(resolved.c_str(), &meta, img);
	}
	return DirectX::LoadFromWICFile(resolved.c_str(), DirectX::WIC_FLAGS_NONE, &meta, img);
}

HRESULT TextureLoader::LoadFromMemory(
	const std::uint8_t* data,
	size_t size,
	DirectX::TexMetadata& meta,
	DirectX::ScratchImage& img)
{
	if (data == nullptr || size == 0) return E_INVALIDARG;
	return DirectX::LoadFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, &meta, img);
}

HRESULT TextureLoader::LoadWic(
	const std::wstring& filePath,
	DirectX::TexMetadata& meta,
	DirectX::ScratchImage& img)
{
	const std::filesystem::path resolved = ResolveAssetPath(filePath);

	if (!std::filesystem::exists(resolved))
	{
		LOG->LogError("TextureLoader::LoadWic: " + resolved.string() + " not found");
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	}
	return DirectX::LoadFromWICFile(resolved.c_str(), DirectX::WIC_FLAGS_NONE, &meta, img);
}
