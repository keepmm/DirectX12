/*! ************************************************************
 * \file   MaterialTextures.cpp
 * \brief  マテリアルが持つテクスチャ群と SRV ヒープの管理
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 Material から分離して作成
 * *********************************************************************/
#include "MaterialTextures.hpp"

#include <cstring>
#include <DirectXTex.h>

#include "d3dx12.h"
#include "DirectX.hpp"
#include "Logger.hpp"
#include "ShaderTypes.hpp"
#include "TextureLoader.hpp"

// ------------------------------------------------------------------ //
//                            ヒープ / SRV                             //
// ------------------------------------------------------------------ //

bool MaterialTextures::EnsureHeap()
{
	if (m_Heap != nullptr) return true;

	auto device = APP->GetDevice();
	if (device == nullptr) return false;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = TexSlot::Count;	// t0..t7 / t8(反射) / t9(AO)
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_Heap.GetAddressOf()))))
	{
		return false;
	}

	// 全slotを null descriptor で初期化（未設定slotをサンプルしても0が返るので安全）
	D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
	nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullSrv.Texture2D.MipLevels = 1;

	const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_Heap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < TexSlot::Count; ++i)
	{
		device->CreateShaderResourceView(nullptr, &nullSrv, cpu);
		cpu.ptr += inc;
	}
	return true;
}

void MaterialTextures::CreateSrv(UINT slot, DXGI_FORMAT format, UINT mipLevels, ID3D12Resource* resource)
{
	auto device = APP->GetDevice();
	if (device == nullptr || m_Heap == nullptr) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = mipLevels;

	const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_Heap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(slot) * inc;
	device->CreateShaderResourceView(resource, &srvDesc, cpu);
}

bool MaterialTextures::CreateResources(
	TextureSlot& s, const D3D12_RESOURCE_DESC& texDesc, UINT64& outRowSize)
{
	auto device = APP->GetDevice();
	if (device == nullptr) return false;

	// テクスチャ本体(DEFAULTヒープ)
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(s.texture.ReleaseAndGetAddressOf()))))
	{
		LOG->LogError("MaterialTextures: CreateCommittedResource failed");
		return false;
	}

	// フットプリント取得 → アップロードバッファ
	UINT numRows = 0;
	UINT64 uploadSize = 0;
	outRowSize = 0;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0, &s.footprint, &numRows, &outRowSize, &uploadSize);

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(s.upload.ReleaseAndGetAddressOf()))))
	{
		LOG->LogError("MaterialTextures: create upload buffer failed");
		return false;
	}
	return true;
}

// ------------------------------------------------------------------ //
//                            テクスチャ生成                           //
// ------------------------------------------------------------------ //

bool MaterialTextures::CreateWhite()
{
	if (!EnsureHeap()) return false;

	constexpr UINT width = 2;
	constexpr UINT height = 2;
	constexpr UINT pixelSize = 4;	// RGBA8なので1ピクセルあたり4バイト
	constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

	const std::array<std::uint8_t, width * height * pixelSize> pixels =
	{
		255,255,255,255, 255,255,255,255,
		255,255,255,255, 255,255,255,255
	};

	return CreateFromRGBA(TexSlot::Albedo, width, height, pixels.data(), format);
}

bool MaterialTextures::SetSolidColor(const COLOR& color)
{
	// CreateWhite() が 2x2 の白を作っているので、その中身を塗り替えるだけで済む。
	// リソースを作り直さないので、ホバー色の切り替えのように毎フレーム呼ばれても安全
	TextureSlot& s = m_Slots[TexSlot::Albedo];
	if (s.texture == nullptr || s.upload == nullptr)
	{
		if (!CreateWhite()) return false;
		if (s.texture == nullptr || s.upload == nullptr) return false;
	}

	auto to8 = [](float v)
		{
			const float c = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
			return static_cast<std::uint8_t>(c * 255.0f + 0.5f);
		};
	const std::uint8_t px[4] = { to8(color.x), to8(color.y), to8(color.z), to8(color.w) };

	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (FAILED(s.upload->Map(0, &readRange, &mapped))) return false;

	constexpr UINT kWidth = 2;
	constexpr UINT kHeight = 2;
	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	for (UINT y = 0; y < kHeight; ++y)
	{
		auto* row = dst + static_cast<size_t>(y) * s.footprint.Footprint.RowPitch;
		for (UINT x = 0; x < kWidth; ++x)
		{
			std::memcpy(row + static_cast<size_t>(x) * 4, px, 4);
		}
	}
	s.upload->Unmap(0, nullptr);

	// 実際のコピーは Flush が描画時のコマンドリストに積む
	s.pending = true;
	return true;
}

void MaterialTextures::CreateDefaultRamp()
{
	if (m_Heap == nullptr) return;	// CreateWhite でヒープ作成済みの前提

	// 4x1ピクセル: 左半分=影色、右半分=明色
	constexpr UINT rampW = 4;
	static const std::uint8_t rampPixels[rampW * 4] =
	{
		 60,  60,  70, 255,
		 60,  60,  70, 255,
		255, 255, 255, 255,
		255, 255, 255, 255,
	};

	CreateFromRGBA(TexSlot::Ramp, rampW, 1, rampPixels, DXGI_FORMAT_R8G8B8A8_UNORM);
}

bool MaterialTextures::CreateFromRGBA(
	UINT slot, UINT width, UINT height, const std::uint8_t* rgba, DXGI_FORMAT format)
{
	if (rgba == nullptr || width == 0 || height == 0) return false;
	if (!EnsureHeap()) return false;

	constexpr UINT pixelSize = 4;
	TextureSlot& s = m_Slots[slot];

	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
	UINT64 rowSize = 0;
	if (!CreateResources(s, texDesc, rowSize)) return false;

	// ピクセルをアップロードバッファへ(行ピッチを合わせてコピー)
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (SUCCEEDED(s.upload->Map(0, &readRange, &mapped)))
	{
		auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
		const UINT srcRowPitch = width * pixelSize;
		for (UINT y = 0; y < height; ++y)
		{
			std::memcpy(dst + static_cast<size_t>(y) * s.footprint.Footprint.RowPitch,
				rgba + static_cast<size_t>(y) * srcRowPitch, srcRowPitch);
		}
		s.upload->Unmap(0, nullptr);
		s.pending = true;	// 実コピーは Flush で
	}

	CreateSrv(slot, format, 1, s.texture.Get());
	s.valid = true;
	return true;
}

bool MaterialTextures::UploadImage(
	UINT slot, const DirectX::Image* srcImage, const DirectX::TexMetadata& metadata, bool srgb)
{
	if (srcImage == nullptr) return false;
	if (!EnsureHeap()) return false;

	TextureSlot& s = m_Slots[slot];

	// ベースカラーは sRGB として読む。法線 / メタル / ラフはリニアのまま
	const DXGI_FORMAT format = srgb ? DirectX::MakeSRGB(metadata.format) : metadata.format;

	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height),
		static_cast<UINT16>(metadata.arraySize),
		static_cast<UINT16>(metadata.mipLevels));

	UINT64 rowSize = 0;
	if (!CreateResources(s, texDesc, rowSize)) return false;

	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);	// 読み取りはしないので範囲は0
	if (FAILED(s.upload->Map(0, &readRange, &mapped)))
	{
		LOG->LogError("MaterialTextures: upload buffer map failed");
		return false;
	}

	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	const size_t copyBytes = (srcImage->rowPitch < static_cast<size_t>(rowSize))
		? srcImage->rowPitch : static_cast<size_t>(rowSize);
	for (UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * s.footprint.Footprint.RowPitch,
			srcImage->pixels + static_cast<size_t>(y) * srcImage->rowPitch,
			copyBytes);
	}
	s.upload->Unmap(0, nullptr);

	CreateSrv(slot, format, static_cast<UINT>(metadata.mipLevels), s.texture.Get());
	s.pending = true;
	s.valid = true;
	return true;
}

bool MaterialTextures::LoadFromFile(UINT slot, const std::wstring& path, bool srgb)
{
	if (!EnsureHeap()) return false;

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (FAILED(TextureLoader::LoadAny(path, metadata, image))) return false;

	return UploadImage(slot, image.GetImage(0, 0, 0), metadata, srgb);
}

void MaterialTextures::ShareSlot(UINT slot, const MaterialTextures& src, UINT srcSlot)
{
	const TextureSlot& from = src.m_Slots[srcSlot];
	if (!from.texture || !EnsureHeap()) return;

	TextureSlot& s = m_Slots[slot];
	s.texture = from.texture;	// リソース共有(ComPtrなので参照カウント)
	s.upload.Reset();
	s.pending = false;			// アップロードは共有元が実施済み
	s.valid = true;
	m_SourcePaths[slot] = src.m_SourcePaths[srcSlot];

	// ミップ0しかアップロードしていないので1固定(desc.MipLevelsだと未初期化ミップを露出する)
	CreateSrv(slot, s.texture->GetDesc().Format, 1, s.texture.Get());
}

// ------------------------------------------------------------------ //
//                          アップロードの実行                         //
// ------------------------------------------------------------------ //

void MaterialTextures::Flush(ID3D12GraphicsCommandList* commandList)
{
	if (commandList == nullptr) return;

	for (TextureSlot& s : m_Slots)
	{
		if (!s.pending || s.texture == nullptr || s.upload == nullptr) continue;

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = s.texture.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = s.upload.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = s.footprint;

		commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			s.texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		commandList->ResourceBarrier(1, &barrier);

		s.pending = false;	// ← コピーした時だけfalse
		APP->DeferRelease(std::move(s.upload));
	}
}

// ------------------------------------------------------------------ //
//                      APP 所有リソースのバインド                     //
// ------------------------------------------------------------------ //

void MaterialTextures::BindGlobals(float reflectStrength)
{
	BindEnvironmentIfNeeded();
	BindShadowMapIfNeeded();

	// 床など反射を使うマテリアルだけが対象。強度0なら張らない
	if (reflectStrength > 0.0f) BindReflectionIfNeeded();
}

void MaterialTextures::BindEnvironmentIfNeeded()
{
	if (m_EnvBound && m_EnvGen == APP->GetEnvGeneration()) return;
	if (!EnsureHeap()) return;

	auto* env = APP->GetEnvTexture();
	if (!env) return;

	CreateSrv(TexSlot::Environment,
		DXGI_FORMAT_R32G32B32A32_FLOAT,	// HDRのフォーマットに合わせる
		APP->GetEnvMipLevels(), env);

	m_EnvMaxMip = float(APP->GetEnvMipLevels() - 1);
	m_EnvBound = true;
	m_EnvGen = APP->GetEnvGeneration();
}

void MaterialTextures::BindShadowMapIfNeeded()
{
	if (m_ShadowBound || m_Heap == nullptr) return;

	auto* sm = APP->GetShadowMap().GetResource();
	if (!sm) return;

	// TYPELESS深度をR32として読む
	CreateSrv(TexSlot::ShadowMap, DXGI_FORMAT_R32_FLOAT, 1, sm);
	m_ShadowBound = true;
}

void MaterialTextures::BindReflectionIfNeeded()
{
	if (m_Heap == nullptr) return;

	auto* rt = APP->GetReflectionRT().GetResource().Get();
	const UINT gen = APP->GetReflectionGeneration();
	if (rt == nullptr || (rt == m_ReflectionBound && gen == m_ReflectionGen)) return;

	CreateSrv(TexSlot::Reflection, DXGI_FORMAT_R8G8B8A8_UNORM, 1, rt);

	// RTを作り直したら張り直す必要があるので、相手を覚えておく
	m_ReflectionBound = rt;
	m_ReflectionGen = gen;
}

// ------------------------------------------------------------------ //
//                          定数バッファへの反映                       //
// ------------------------------------------------------------------ //

void MaterialTextures::FillCB(MaterialCB& out) const
{
	// w>0 なら環境あり
	out.mapFlags = { Valid(TexSlot::Normal) ? 1.0f : 0.0f,
					 Valid(TexSlot::Metal)  ? 1.0f : 0.0f,
					 Valid(TexSlot::Rough)  ? 1.0f : 0.0f,
					 m_EnvMaxMip };

	out.pbrParams.x = Valid(TexSlot::Emissive)  ? 1.0f : 0.0f;
	out.pbrParams.y = Valid(TexSlot::Occlusion) ? 1.0f : 0.0f;

	// 既定ランプなら算術で済ませる
	out.faceParam.z = m_HasCustomRamp ? 1.0f : 0.0f;
}
