#include "Material.hpp"
#include "d3dx12.h"
#include "DirectXTex/DirectXTex.h"
#pragma comment(lib, "DirectXTex.lib")
#include "Logger.hpp"

void Material::Init(
	const ComPtr<ID3D12Device>& device, 
	const DirectXApp::PipelineStateTable& pipelinestates, 
	const ComPtr<ID3D12PipelineState>& wirePso)
{
	// 引数のどれかが空の場合初期化しない
	if(device == nullptr) return;

	m_Device = device;

	CreateCheckerTexture(device);

	m_PipelineStates = pipelinestates;
	m_WirePso = wirePso;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	const UINT bufferSize = CB_SIZE * MAX_ENTITY_PER_FRAME;
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	for (UINT i = 0; i < FRAME_COUNT; ++i)
	{
		HRESULT hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_ConstantBuffer[i]));
		if (FAILED(hr))
		{
			m_ConstantBuffer[i] = nullptr;
			m_MappedData[i] = nullptr;
			continue;
		}

		void* mapped = nullptr;
		CD3DX12_RANGE readRange(0, 0); // 読み取りはしないので範囲は0
		const HRESULT mapHr = m_ConstantBuffer[i]->Map(0, &readRange, &mapped);
		if (FAILED(mapHr))
		{
			m_ConstantBuffer[i]->Release();
			m_ConstantBuffer[i] = nullptr;
			m_MappedData[i] = nullptr;
		}

		m_MappedData[i] = reinterpret_cast<std::uint8_t*>(mapped);
	}
}

bool Material::SetTextureFromFile(const std::wstring& filePath)
{
	// デバイスがnullptrの場合は処理しない
	if (m_Device == nullptr) {
		LOG->LogError("SetTextureFromFile: device is null");
		return false;
	}

	std::filesystem::path resolved = filePath;
	if (resolved.is_relative())
	{
		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		resolved = std::filesystem::path(exePath).parent_path() / resolved;
	}

	if (!std::filesystem::exists(resolved))
	{
		LOG->LogError("SetTextureFromFile: texture not found");
		return false;
	}

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	const HRESULT hr = DirectX::LoadFromWICFile(
		resolved.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);

	// 読み込みに失敗
	if (FAILED(hr)) {
		LOG->LogHRESULT(hr, "LoadFromWICFile failed");
		return false;
	}

	const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
	if (srcImage == nullptr)
	{
		LOG->LogError("SetTextureFromFile: image.GetImage returned null");
		return false;
	}

	if (m_TextureSrvHeap == nullptr)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		if (FAILED(m_Device->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(m_TextureSrvHeap.GetAddressOf()))))
		{
			LOG->LogError("SetTextureFromFile: CreateDescriptorHeap failed");
			return false;
		}
	}

	// テクスチャのサイズとフォーマットを定義
	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height),
		static_cast<UINT16>(metadata.arraySize),
		static_cast<UINT16>(metadata.mipLevels)
	);

	// テクスチャリソースを作成
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(m_Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_Texture.GetAddressOf()))))
	{
		LOG->LogError("SetTextureFromFile: CreateCommittedResource failed");
		return false;
	}

	// フットプリントを取得してアップロード用のバッファを作成
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 uploadSize = 0;
	m_Device->GetCopyableFootprints(
		&texDesc,
		0,
		1,
		0,
		&footprint,
		&numRows,
		&rowSizeInBytes,
		&uploadSize);

	m_TextureFootprint = footprint;

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(m_Device->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_TextureUpload.GetAddressOf()))))
	{
		LOG->LogError("SetTextureFromFile: Create upload buffer failed");
		return false;
	}

	// マップしてテクスチャデータをコピー
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0); // 読み取りはしないので範囲は0
	if (FAILED(m_TextureUpload->Map(0, &readRange, &mapped))) {
		LOG->LogError("SetTextureFromFile: upload buffer map failed");
		return false;
	}

	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	const size_t copyBytes = (srcImage->rowPitch < static_cast<size_t>(rowSizeInBytes))
		? srcImage->rowPitch
		: static_cast<size_t>(rowSizeInBytes);
	for (UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * m_TextureFootprint.Footprint.RowPitch,
			srcImage->pixels + static_cast<size_t>(y) * srcImage->rowPitch,
			copyBytes);
	}
	m_TextureUpload->Unmap(0, nullptr);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

	m_Device->CreateShaderResourceView(
		m_Texture.Get(),
		&srvDesc,
		m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart());

	m_TextureUploadPending = true;

	LOG->LogInfo("SetTextureFromFile: texture loaded");
	return true;
}

bool Material::SetTextureFromMemory(const std::uint8_t* data, size_t size)
{
	// デバイスがnullptrの場合は処理しない
	if (m_Device == nullptr)
	{
		LOG->LogError("SetTextureFromMemory: device is null");
		return false;
	}

	// データがnullptrまたはサイズが0の場合は処理しない
	if (data == nullptr || size == 0)
	{
		LOG->LogError("SetTextureFromMemory: invalid data");
		return false;
	}

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	const HRESULT hr = DirectX::LoadFromWICMemory(
		data,size,DirectX::WIC_FLAGS_NONE,&metadata,image);

	if (FAILED(hr))
	{
		LOG->LogHRESULT(hr, "LoadFromWICMemory failed");
		return false;
	}

	const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
	if (srcImage == nullptr)
	{
		LOG->LogError("SetTextureFromMemory: image.GetImage returned null");
		return false;
	}

	if (m_TextureSrvHeap == nullptr)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		if (FAILED(m_Device->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(m_TextureSrvHeap.GetAddressOf()))))
		{
			LOG->LogError("SetTextureFromFile: CreateDescriptorHeap failed");
			return false;
		}
	}

	// テクスチャのサイズとフォーマットを定義
	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height),
		static_cast<UINT16>(metadata.arraySize),
		static_cast<UINT16>(metadata.mipLevels)
	);

	// テクスチャリソースを作成
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(m_Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_Texture.GetAddressOf()))))
	{
		LOG->LogError("SetTextureFromFile: CreateCommittedResource failed");
		return false;
	}

	// フットプリントを取得してアップロード用のバッファを作成
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 uploadSize = 0;
	m_Device->GetCopyableFootprints(
		&texDesc,
		0,
		1,
		0,
		&footprint,
		&numRows,
		&rowSizeInBytes,
		&uploadSize);

	m_TextureFootprint = footprint;

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(m_Device->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_TextureUpload.GetAddressOf()))))
	{
		LOG->LogError("SetTextureFromFile: Create upload buffer failed");
		return false;
	}

	// マップしてテクスチャデータをコピー
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0); // 読み取りはしないので範囲は0
	if (FAILED(m_TextureUpload->Map(0, &readRange, &mapped))) {
		LOG->LogError("SetTextureFromFile: upload buffer map failed");
		return false;
	}

	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	const size_t copyBytes = (srcImage->rowPitch < static_cast<size_t>(rowSizeInBytes))
		? srcImage->rowPitch
		: static_cast<size_t>(rowSizeInBytes);
	for (UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * m_TextureFootprint.Footprint.RowPitch,
			srcImage->pixels + static_cast<size_t>(y) * srcImage->rowPitch,
			copyBytes);
	}
	m_TextureUpload->Unmap(0, nullptr);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

	m_Device->CreateShaderResourceView(
		m_Texture.Get(),
		&srvDesc,
		m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart());

	m_TextureUploadPending = true;

	LOG->LogInfo("SetTextureFromFile: texture loaded");
	return true;
}

void Material::Apply(
	ID3D12GraphicsCommandList* commandList, 
	const float4x4& world, 
	const float4x4& view, 
	const float4x4& projection, 
	bool wireframe,
	E_VERTEX_SHADER vsType,
	E_PIXEL_SHADER psType,
	ID3D12PipelineState* overridePso,
	UINT frameIndex)
{

	// コマンドリストが空の場合は適用しない
	if (commandList == nullptr) {
		return;
	}

	UpdateTextureIfNeeded(commandList);

	if (m_TextureSrvHeap != nullptr)
	{
		ID3D12DescriptorHeap* heaps[] = { m_TextureSrvHeap.Get() };
		commandList->SetDescriptorHeaps(_countof(heaps), heaps);
		commandList->SetGraphicsRootDescriptorTable(1, m_TextureSrvHeap->GetGPUDescriptorHandleForHeapStart());
	}

	// パイプラインステートオブジェクトを設定
	const UINT frameSlot = frameIndex % FRAME_COUNT;
	if (m_ConstantBuffer[frameSlot] == nullptr)
	{
		return;
	}

	// フレームごとのエンティティ数を管理し、一定数を超えたらそれ以上追加しない
	if (m_LastFrameIndex != frameIndex) {
		m_EntityCountPerFrame[frameSlot] = 0;
		m_LastFrameIndex = frameIndex;
	}

	// エンティティ数が一定数を超えたら追加しない
	if(m_EntityCountPerFrame[frameSlot] >= MAX_ENTITY_PER_FRAME) {
		return;
	}

	// 定数バッファにデータを書き込む
	TransformBuffer data{};
	BuildBufferData(world, view, projection, &data);

	const UINT entityIndex = m_EntityCountPerFrame[frameSlot]++;
	const UINT offset = entityIndex * CB_SIZE;

	std::memcpy(m_MappedData[frameSlot] + offset, &data, sizeof(TransformBuffer));

	const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress =
		m_ConstantBuffer[frameSlot]->GetGPUVirtualAddress() + offset;
	commandList->SetGraphicsRootConstantBufferView(0, gpuAddress);

	ID3D12PipelineState* selectedPso = overridePso;
	if (selectedPso == nullptr)
	{
		if (wireframe)
		{
			selectedPso = m_WirePso.Get();
		}
		else
		{
			const size_t vsIndex = static_cast<size_t>(vsType);
			const size_t psIndex = static_cast<size_t>(psType);
			selectedPso = m_PipelineStates[vsIndex][psIndex].Get();
		}
	}

	if (selectedPso == nullptr)
	{
		return;
	}

	commandList->SetPipelineState(selectedPso);
}

void Material::BuildBufferData(
	const float4x4& world, 
	const float4x4& view, 
	const float4x4& projection,
	_Out_ TransformBuffer* outData) const
{
	TransformBuffer data{};

	const auto w = DirectX::XMLoadFloat4x4(&world);
	const auto v = DirectX::XMLoadFloat4x4(&view);
	const auto p = DirectX::XMLoadFloat4x4(&projection);
	const auto wvp = w * v * p;

	DirectX::XMStoreFloat4x4(&data.worldViewProj, DirectX::XMMatrixTranspose(wvp));
	DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixTranspose(w));

	const auto lightDir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_LightDir));
	DirectX::XMStoreFloat4(&data.lightDir, lightDir);
	data.lightColor = m_LightColor;
	data.ambientColor = m_AmbientColor;
	*outData = data;
}

void Material::CreateCheckerTexture(const ComPtr<ID3D12Device>& device)
{
	// deviceがnullptrの場合は処理しない
	if(device == nullptr) {
		return;
	}

	// シェーダーからアクセスするためのSRVを作成するためのディスクリプタヒープを作成
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	// ディスクリプタヒープの作成に失敗した場合は処理しない
	if(FAILED(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(m_TextureSrvHeap.GetAddressOf())))) {
		return;
	}

	// テクスチャのサイズとフォーマットを定義
	constexpr UINT width = 2;
	constexpr UINT height = 2;
	constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr UINT pixelSize = 4; // RGBA8なので1ピクセルあたり4バイト

	// チェッカーテクスチャのピクセルデータ（白と黒の2x2）
	std::array < std::uint8_t, width* height* pixelSize> pixels =
	{
		255,255,255,255, 30,30,30,255,
		30,30,30,255,255,255,255,255
	};

	// テクスチャリソースを作成
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);

	// テクスチャリソースの作成に失敗した場合は処理しない
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_Texture.GetAddressOf()))))
	{
		return;
	}

	// テクスチャにデータをアップロードするためのアップロードバッファを作成
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 uploadSize = 0;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &uploadSize);

	m_TextureFootprint = footprint;

	// アップロードバッファの作成に失敗した場合は処理しない
	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_TextureUpload.GetAddressOf()))))
	{
		return;
	}

	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0); // 読み取りはしないので範囲は04
	// 成功時
	if (SUCCEEDED(m_TextureUpload->Map(0, &readRange, &mapped)))
	{
		auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
		const UINT srcRowPitch = width * pixelSize;
		for (UINT y = 0; y < height; ++y)
		{
			std::memcpy(
				dst + y * footprint.Footprint.RowPitch,
				pixels.data() + y * srcRowPitch,
				srcRowPitch
			);
		}
		m_TextureUpload->Unmap(0, nullptr);
		m_TextureUploadPending = true;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	device->CreateShaderResourceView(m_Texture.Get(), &srvDesc, m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart());

}

void Material::UpdateTextureIfNeeded(ID3D12GraphicsCommandList* commandList)
{
	if (!m_TextureUploadPending || m_Texture == nullptr || m_TextureUpload == nullptr || commandList == nullptr)
	{
		return;
	}

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = m_Texture.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = m_TextureUpload.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = m_TextureFootprint;

	commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_Texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &barrier);

	m_TextureUploadPending = false;
}
