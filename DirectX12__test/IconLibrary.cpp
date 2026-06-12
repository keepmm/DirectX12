#include "IconLibrary.hpp"
#include "DirectX.hpp"
#include "d3dx12.h"
#include "DirectXTex/DirectXTex.h"
#include "Logger.hpp"
#include <filesystem>

ImTextureID IconLibrary::GetOrLoad(const std::wstring& path)
{
	// キャッシュにあれば即返す（失敗キャッシュ=0 も含む）
	auto it = m_Cache.find(path);
	if (it != m_Cache.end())
	{
		return it->second;
	}

	ImTextureID id = 0;
	if (!LoadTexture(path, id))
	{
		// 失敗も記録して毎フレーム再試行しないようにする
		m_Cache[path] = 0;
		return 0;
	}

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

	// ---- WICで読み込み ---- //
	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (FAILED(DirectX::LoadFromWICFile(
		resolved.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image)))
	{
		LOG->LogError("IconLibrary: LoadFromWICFile failed");
		return false;
	}

	const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
	if (srcImage == nullptr)
	{
		return false;
	}

	// ---- テクスチャ本体（DEFAULTヒープ）---- //
	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height));

	Microsoft::WRL::ComPtr<ID3D12Resource> texture;
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

	Microsoft::WRL::ComPtr<ID3D12Resource> upload;
	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(upload.GetAddressOf()))))
	{
		return false;
	}

	// ピクセルコピー
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (FAILED(upload->Map(0, &readRange, &mapped)))
	{
		return false;
	}
	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	const size_t copyBytes = (srcImage->rowPitch < static_cast<size_t>(rowSizeInBytes))
		? srcImage->rowPitch : static_cast<size_t>(rowSizeInBytes);
	for (UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
			srcImage->pixels + static_cast<size_t>(y) * srcImage->rowPitch,
			copyBytes);
	}
	upload->Unmap(0, nullptr);

	// ---- 自前のコマンドリストで即時アップロード ---- //
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
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

	// アップロード完了を待つ（エディタ用の1回きりロードなので同期でよい）
	APP->WaitForGPUIdle();

	// ---- グローバルSRVヒープにSRVを作成 ---- //
	UINT srvIndex = 0;
	if (!APP->GetSrvAllocator().Allocate(srvIndex))
	{
		LOG->LogError("IconLibrary: SRVヒープが満杯です");
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	device->CreateShaderResourceView(
		texture.Get(), &srvDesc, APP->GetSrvAllocator().Cpu(srvIndex));

	// GPUハンドルがそのまま ImTextureID になる（RenderTextureと同じパターン）
	outId = static_cast<ImTextureID>(APP->GetSrvAllocator().Gpu(srvIndex).ptr);

	// テクスチャ本体を保持（ComPtrが解放されないように）
	m_Textures.push_back(texture);

	LOG->LogInfo("IconLibrary: icon loaded");
	return true;
}