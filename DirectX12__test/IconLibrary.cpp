#include "IconLibrary.hpp"
#include "DirectX.hpp"
#include "d3dx12.h"
#include <DirectXTex.h>
#include "Logger.hpp"
#include <filesystem>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment (lib, "windowscodecs.lib")

ImTextureID IconLibrary::GetOrLoad(const std::wstring& path)
{
	// キャッシュにあれば即返す（失敗キャッシュ=0 も含む）
	auto it = m_Cache.find(path);
	if (it != m_Cache.end())
	{
		return it->second;
	}

	if (m_LoadsThisFrame >= kMaxLoadsPerFrame) return 0;
	++m_LoadsThisFrame;

	ImTextureID id = 0;
	if (!LoadTexture(path, id)) { m_Cache[path] = 0; return 0; }
	m_Cache[path] = id;
	return id;
}

bool IconLibrary::LoadTexture(const std::wstring& path, ImTextureID& outId)
{
	outId = 0;

	auto device = APP->GetDevice();
	if (device == nullptr)
	{
		return false;
	}

	// 相対パスはexe基準で解決（Materialと同じ）
	std::filesystem::path resolved = path;
	if (resolved.is_relative())
	{
		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		resolved = std::filesystem::path(exePath).parent_path() / resolved;
	}

	if (!std::filesystem::exists(resolved))
	{
		LOG->LogError("IconLibrary: icon not found");
		return false;
	}

	// ---- WICで「縮小しながら」デコード（4Kを保持しない）---- //
	using Microsoft::WRL::ComPtr;
	constexpr UINT kThumb = 128;

	ComPtr<IWICImagingFactory> wic;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))))
	{
		LOG->LogError("IconLibrary: WIC factory failed");
		return false;
	}

	ComPtr<IWICBitmapDecoder> decoder;
	if (FAILED(wic->CreateDecoderFromFilename(resolved.c_str(), nullptr,
		GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())))
	{
		LOG->LogError("IconLibrary: WIC decoder failed");
		return false;
	}

	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
	{
		return false;
	}

	UINT ow = 0, oh = 0;
	frame->GetSize(&ow, &oh);
	if (ow == 0 || oh == 0)
	{
		return false;
	}

	// アスペクト比維持で最大辺kThumb（元が小さければ等倍）
	UINT tw = ow, th = oh;
	if (ow > kThumb || oh > kThumb)
	{
		if (ow >= oh) { th = std::max<UINT>(1, oh * kThumb / ow); tw = kThumb; }
		else { tw = std::max<UINT>(1, ow * kThumb / oh); th = kThumb; }
	}

	// 縮小
	ComPtr<IWICBitmapScaler> scaler;
	if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())))
	{
		return false;
	}
	if (FAILED(scaler->Initialize(frame.Get(), tw, th, WICBitmapInterpolationModeFant)))
	{
		return false;
	}

	// RGBA8へ変換
	ComPtr<IWICFormatConverter> conv;
	if (FAILED(wic->CreateFormatConverter(conv.GetAddressOf())))
	{
		return false;
	}
	if (FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA,
		WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
	{
		return false;
	}

	// ピクセル取得（小サイズRGBA）
	const UINT rowPitch = tw * 4;
	std::vector<std::uint8_t> pixels(static_cast<size_t>(rowPitch) * th);
	if (FAILED(conv->CopyPixels(nullptr, rowPitch,
		static_cast<UINT>(pixels.size()), pixels.data())))
	{
		return false;
	}

	// 以降で使うメタ情報（縮小後）
	DirectX::TexMetadata metadata{};
	metadata.width = tw;
	metadata.height = th;
	metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// ---- テクスチャ本体（DEFAULTヒープ）---- //
	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height));

	ComPtr<ID3D12Resource> texture;
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(texture.GetAddressOf()))))
	{
		LOG->LogError("IconLibrary: CreateCommittedResource failed");
		return false;
	}

	// ---- アップロードバッファ ---- //
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0, uploadSize = 0;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0,
		&footprint, &numRows, &rowSizeInBytes, &uploadSize);

	ComPtr<ID3D12Resource> upload;
	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(upload.GetAddressOf()))))
	{
		return false;
	}

	// ピクセルコピー（縮小後バッファから）
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (FAILED(upload->Map(0, &readRange, &mapped)))
	{
		return false;
	}
	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	for (UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
			pixels.data() + static_cast<size_t>(y) * rowPitch,
			rowPitch);
	}
	upload->Unmap(0, nullptr);

	// ---- 専用のコマンドリストで即アップロード ---- //
	ComPtr<ID3D12CommandAllocator> cmdAlloc;
	ComPtr<ID3D12GraphicsCommandList> cmdList;
	if (FAILED(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdAlloc.GetAddressOf()))))
	{
		return false;
	}
	if (FAILED(device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr,
		IID_PPV_ARGS(cmdList.GetAddressOf()))))
	{
		return false;
	}

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = texture.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = upload.Get();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = footprint;

	cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &barrier);

	cmdList->Close();

	ID3D12CommandList* lists[] = { cmdList.Get() };
	APP->GetCommandQueue()->ExecuteCommandLists(1, lists);

	// アップロード完了待ち（エディタ用に1回きりロードなので同期でよい）
	APP->WaitForGPUIdle();

	// ---- グローバルSRVヒープにSRV作成 ---- //
	UINT srvIndex = 0;
	if (!APP->GetSrvAllocator().Allocate(srvIndex))
	{
		LOG->LogError("IconLibrary: SRVヒープが枯渇です");
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	device->CreateShaderResourceView(
		texture.Get(), &srvDesc, APP->GetSrvAllocator().Cpu(srvIndex));

	outId = static_cast<ImTextureID>(APP->GetSrvAllocator().Gpu(srvIndex).ptr);

	m_Textures.push_back(texture);

	LOG->LogInfo("IconLibrary: icon loaded");
	return true;
}