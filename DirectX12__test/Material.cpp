#include "Material.hpp"
#include "d3dx12.h"
#include <DirectXTex.h>
#include "Logger.hpp"
#include "ConstantBufferAllocator.hpp"
#include "Util.hpp"

/// @brief テクスチャの読み込み(拡張子に応じて自動判別)
/// @param filePath ファイルのパス
/// @param meta metadata
/// @param img scrachimage
/// @return 成功時はS_OK、失敗時はエラーコード
static HRESULT LoadImgAny(
	_In_ const std::wstring& filePath, 
	_In_ DirectX::TexMetadata& meta, 
	_In_ DirectX::ScratchImage& img)
{
	HRESULT hr;

	std::filesystem::path resolved = ResolveAssetPath(filePath);

	if (!std::filesystem::exists(resolved))
	{
		LOG->LogError("SetTextureFromFile: " + resolved.string() + " not found");
		return false;
	}

	const std::wstring ext = resolved.extension().wstring();
	auto iequals = [](std::wstring a, const wchar_t* b)
		{
			std::transform(a.begin(), a.end(), a.begin(), ::towlower);
			return a == b;
		};

	if (iequals(ext, L".hdr"))
	{
		hr = DirectX::LoadFromHDRFile(resolved.c_str(), &meta, img);
	}
	else if (iequals(ext, L".dds"))
	{
		hr = DirectX::LoadFromDDSFile(resolved.c_str(), DirectX::DDS_FLAGS_NONE, &meta, img);
	}
	else if (iequals(ext, L".tga"))
	{
		hr = DirectX::LoadFromTGAFile(resolved.c_str(), &meta, img);
	}
	else
	{
		hr = DirectX::LoadFromWICFile(resolved.c_str(), DirectX::WIC_FLAGS_NONE, &meta, img);
	}

	return hr;
}

void Material::Init()
{
	// 引数のどれかが空の場合初期化しない
	if(APP->GetDevice() == nullptr) return;

	CreateCheckerTexture(APP->GetDevice());
	CreateDefaultRampTexture();

	m_PipelineStates = APP->GetPipelineStates();
	m_WirePso = APP->GetPipelineStateWireFrame();
}

bool Material::SetTextureFromFile(const std::wstring& filePath)
{
	// デバイスがnullptrの場合は処理しない
	if (APP->GetDevice() == nullptr) {
		LOG->LogError("SetTextureFromFile: device is null");
		return false;
	}

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};

	// 読み込みに失敗
	if (FAILED(LoadImgAny(filePath, metadata, image))) {
		LOG->LogError("LoadFromWICFile failed");
		return false;
	}	

	const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
	if (srcImage == nullptr)
	{
		LOG->LogError("SetTextureFromFile: image.GetImage returned null");
		return false;
	}

	if (!EnsureSrvHeap()) return false;

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
	if (FAILED(APP->GetDevice()->CreateCommittedResource(
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
	APP->GetDevice()->GetCopyableFootprints(
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
	if (FAILED(APP->GetDevice()->CreateCommittedResource(
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

	APP->GetDevice()->CreateShaderResourceView(
		m_Texture.Get(),
		&srvDesc,
		m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart());

	m_TextureUploadPending = true;

	LOG->LogInfo("SetTextureFromFile: texture loaded");
	return true;
}

bool Material::SetTextureFromMemory(const std::uint8_t* data, size_t size)
{
	if (APP->GetDevice() == nullptr) { LOG->LogError("SetTextureFromMemory: device is null"); return false; }
	if (data == nullptr || size == 0) { LOG->LogError("SetTextureFromMemory: invalid data"); return false; }

	// WICで読み込む（メモリから）
	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (FAILED(DirectX::LoadFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, &metadata, image)))
	{
		LOG->LogError("SetTextureFromMemory: LoadFromWICMemory failed");
		return false;
	}
	const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
	if (srcImage == nullptr) return false;

	return UploadTextureData(srcImage, metadata);
}

bool Material::SetToonRampTexture(const std::wstring& filepath)
{
	if (APP->GetDevice() == nullptr) return false;
	if (m_TextureSrvHeap == nullptr) return false;

	std::filesystem::path resolved = ResolveAssetPath(filepath);

	if (!std::filesystem::exists(resolved))
	{
		LOG->LogError("SetToonRampTexture:ファイルが見つかりません");
		return false;
	}

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	const HRESULT hr = DirectX::LoadFromWICFile(
		resolved.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);
	if(FAILED(hr))
	{
		LOG->LogError("SetToonRampTexture: LoadFromWICFileが失敗しました");
		return false;
	}
	const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
	if (srcImage == nullptr) return false;

	// スロット1(t1)ぬアアプロード
	return UploadTextureTo(
		srcImage,
		metadata,
		1,
		m_RampTexture,
		m_RampUpload,
		m_RampFootprint,
		m_RampUploadPending);
}

/// @brief マテリアルを適用する
/// @param commandList コマンドリスト
/// @param world world行列
/// @param view view行列
/// @param projection proj行列
/// @param wireframe wireframe描画するか
/// @param frameIndex 描画インデックス
/// @param cbAlloc 定数バッファアロケータ
/// @param shaderName シェーダーの名前
/// @param overridePso 使用するPSO
void Material::Apply(
	ID3D12GraphicsCommandList* commandList,
	const float4x4& world,
	const float4x4& view,
	const float4x4& projection,
	bool wireframe,
	UINT frameIndex,
	ConstantBufferAllocator* cbAlloc,
	std::string shaderName,
	ID3D12PipelineState* overridePso
	)
{

	// コマンドリストが空の場合は適用しない
	if (commandList == nullptr) {
		return;
	}

	UpdateTextureIfNeeded(commandList);
	BindEnvironmentIfNeeded();
	BindShadowMapIfNeeded();

	if (m_TextureSrvHeap != nullptr)
	{
		ID3D12DescriptorHeap* heaps[] = { m_TextureSrvHeap.Get() };
		commandList->SetDescriptorHeaps(_countof(heaps), heaps);
		commandList->SetGraphicsRootDescriptorTable(4, m_TextureSrvHeap->GetGPUDescriptorHandleForHeapStart());
	}

	// ------------------------------- //
	// 定数バッファ(アロケータから確保 //
	// ------------------------------- //
	if (cbAlloc == nullptr)
	{
		return;
	}

	// フレームごとの定数バッファを構築してアップロード
	const UINT frameSlot = frameIndex % FRAME_COUNT;

	// -------------------- //
	//  定数バッファを構築  //
	// -------------------- //

	// b0
	FrameCB fdata = {};
	BuildPerFrame(view, projection, &fdata);
	// 定数バッファアロケータからGPU仮想アドレスを取得
	const D3D12_GPU_VIRTUAL_ADDRESS b0 = cbAlloc->Allocate(frameSlot, &fdata, sizeof(FrameCB));

	// b1
	ObjectCB odata = {};
	BuildPerObject(world, &odata);
	const D3D12_GPU_VIRTUAL_ADDRESS b1 = cbAlloc->Allocate(frameSlot, &odata, sizeof(ObjectCB));

	// b3
	MaterialCB mdata{};
	mdata.mapFlags = { m_HasNormal ? 1.f : 0.f, m_HasMetal ? 1.f : 0.f,
					   m_HasRough ? 1.f : 0.f, m_EnvMaxMip };   // w>0 なら環境あり
	mdata.roughness = roughness;
	mdata.faceParam.y = baseAlpha;
	mdata.metallic = metallic;
	mdata.rimColor = rimColor;
	mdata.sssParams = { sssStrength, sssWrap, sssTrans,sheen };
	mdata.sssColor = sssColor;
	mdata.basecolor = baseColor;
	const D3D12_GPU_VIRTUAL_ADDRESS b3 = cbAlloc->Allocate(frameSlot, &mdata, sizeof(MaterialCB));


	if (b0 == 0 || b1 == 0 || b3 == 0)
	{
		// リングが溢れた等で確保できない場合は描画しない
		return;
	}

	// ルートパラメータに定数バッファのGPU仮想アドレスをセット
	commandList->SetGraphicsRootConstantBufferView(0, b0);
	commandList->SetGraphicsRootConstantBufferView(1, b1);
	commandList->SetGraphicsRootConstantBufferView(3, b3);

	// PSOの選択
	ID3D12PipelineState* pso = overridePso;
	std::string name = "Basic";
	if (!pso && wireframe)            pso = m_WirePso.Get();
	if (!pso)                         pso = APP->GetPipelineStateByName(shaderName);
	if (!pso)                         pso = APP->GetPipelineStateByName(name); // 安全網
	if (!pso) return;
	commandList->SetPipelineState(pso);
}

void Material::ShareDiffuseTexture(const Material& src)
{
	if (!src.m_Texture || !EnsureSrvHeap()) return;

	m_Texture = src.m_Texture;   // リソース共有(ComPtrなので参照カウント)
	m_TextureUploadPending = false;   // アップロードは共有元が実施済み

	const auto desc = m_Texture->GetDesc();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	APP->GetDevice()->CreateShaderResourceView(
		m_Texture.Get(), &srvDesc,
		m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Material::BuildPerFrame(const float4x4& view, const float4x4& projection, FrameCB* out) const
{
	FrameCB data{};
	const auto v = DirectX::XMLoadFloat4x4(&view);
	const auto p = DirectX::XMLoadFloat4x4(&projection);
	DirectX::XMStoreFloat4x4(&data.viewProj, DirectX::XMMatrixTranspose(v * p));

	// カメラのワールド座標 = view の逆行列の平行移動
	const auto invV = DirectX::XMMatrixInverse(nullptr, v);
	DirectX::XMFLOAT4X4 iv; DirectX::XMStoreFloat4x4(&iv, invV);
	data.cameraPos = { iv._41, iv._42, iv._43, 1.0f };

	*out = data;
}

void Material::BuildPerObject(const float4x4& world, ObjectCB* out) const
{
	ObjectCB data{};

	// 行列の作成
	const auto w = DirectX::XMLoadFloat4x4(&world);

	// 転置して格納
	DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixTranspose(w));

	*out = data;
}

void Material::CreateCheckerTexture(const ComPtr<ID3D12Device>& device)
{
	// deviceがnullptrの場合は処理しない
	if(device == nullptr) {
		return;
	}

	// シェーダーからアクセスするためのSRVを作成するためのディスクリプタヒープを作成;
	if (!EnsureSrvHeap()) return ;

	// テクスチャのサイズとフォーマットを定義
	constexpr UINT width = 2;
	constexpr UINT height = 2;
	constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr UINT pixelSize = 4; // RGBA8なので1ピクセルあたり4バイト

	// チェッカーテクスチャのピクセルデータ（白と黒の2x2）
	//std::array < std::uint8_t, width* height* pixelSize> pixels =
	//{
	//	255,255,255,255, 30,30,30,255,
	//	30,30,30,255,255,255,255,255
	//};
	std::array < std::uint8_t, width* height* pixelSize> pixels =
	{
		255,255,255,255, 255,255,255,255,
		255,255,255,255, 255,255,255,255
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

	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	//cpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	device->CreateShaderResourceView(m_Texture.Get(), &srvDesc, cpu);

}

void Material::UpdateTextureIfNeeded(ID3D12GraphicsCommandList* commandList)
{
	if (commandList == nullptr) return;


	// --- diffuse（pending時のみコピー）---
	if (m_TextureUploadPending && m_Texture != nullptr && m_TextureUpload != nullptr)
	{
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

		m_TextureUploadPending = false;   // ← コピーした時だけfalse
		APP->DeferRelease(std::move(m_TextureUpload));
	}

	// --- ランプ ---
	if (m_RampUploadPending && m_RampTexture != nullptr && m_RampUpload != nullptr)
	{
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = m_RampTexture.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = m_RampUpload.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = m_RampFootprint;

		commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_RampTexture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		commandList->ResourceBarrier(1, &barrier);

		m_RampUploadPending = false;
		APP->DeferRelease(std::move(m_RampUpload));
	}

	// --- normal / metal / rough（共通flush）---
	auto flush = [&](bool& pending,
		const ComPtr<ID3D12Resource>& tex,
		ComPtr<ID3D12Resource>& upload,
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp)
		{
			if (!pending || tex == nullptr || upload == nullptr) return;

			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = tex.Get();
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = 0;

			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = upload.Get();
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = fp;

			commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				tex.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			commandList->ResourceBarrier(1, &barrier);

			pending = false;
			APP->DeferRelease(std::move(upload));
		};

	flush(m_NormalPending, m_NormalTexture, m_NormalUpload, m_NormalFootprint);
	flush(m_MetalPending, m_MetalTexture, m_MetalUpload, m_MetalFootprint);
	flush(m_RoughPending, m_RoughTexture, m_RoughUpload, m_RoughFootprint);
}

bool Material::CreateTextureFromRGBA(
	UINT width, UINT height, const std::uint8_t* rgba)
{
	auto device = APP->GetDevice();
	if (device == nullptr || rgba == nullptr || width == 0 || height == 0) return false;

	constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr UINT pixelSize = 4;

	// SRVヒープ（無ければ作る）
	if (!EnsureSrvHeap()) return false;

	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_Texture.ReleaseAndGetAddressOf()))))
		return false;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0; UINT64 rowSize = 0, uploadSize = 0;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadSize);
	m_TextureFootprint = footprint;

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_TextureUpload.ReleaseAndGetAddressOf()))))
		return false;

	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (SUCCEEDED(m_TextureUpload->Map(0, &readRange, &mapped)))
	{
		auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
		const UINT srcRowPitch = width * pixelSize;
		for (UINT y = 0; y < height; ++y)
			std::memcpy(dst + y * footprint.Footprint.RowPitch,
				rgba + y * srcRowPitch, srcRowPitch);
		m_TextureUpload->Unmap(0, nullptr);
		m_TextureUploadPending = true; 
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateShaderResourceView(m_Texture.Get(), &srvDesc, cpu);
	return true;
}

bool Material::CreateMapFromRGBA(
	UINT slot,
	UINT width, UINT height, const std::uint8_t* rgba,
	ComPtr<ID3D12Resource>& outTexture,
	ComPtr<ID3D12Resource>& outUpload,
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT& outFootprint,
	bool& outPending)
{
	auto device = APP->GetDevice();
	if (device == nullptr || rgba == nullptr || width == 0 || height == 0) return false;
	if (!EnsureSrvHeap()) return false;   // 7枚ヒープ確保（t0..t6）

	constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr UINT pixelSize = 4;

	// テクスチャ本体（DEFAULTヒープ）
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(outTexture.ReleaseAndGetAddressOf()))))
		return false;

	// フットプリント取得＆アップロードバッファ
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0; UINT64 rowSize = 0, uploadSize = 0;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadSize);
	outFootprint = footprint;

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(outUpload.ReleaseAndGetAddressOf()))))
		return false;

	// ピクセルをアップロードバッファへ（行ピッチを合わせてコピー）
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (SUCCEEDED(outUpload->Map(0, &readRange, &mapped)))
	{
		auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
		const UINT srcRowPitch = width * pixelSize;
		for (UINT y = 0; y < height; ++y)
			std::memcpy(dst + y * footprint.Footprint.RowPitch,
				rgba + y * srcRowPitch, srcRowPitch);
		outUpload->Unmap(0, nullptr);
		outPending = true;   // 実コピーは UpdateTextureIfNeeded で
	}

	// SRV を slot 番目に作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(slot) * inc;
	device->CreateShaderResourceView(outTexture.Get(), &srvDesc, cpu);

	return true;
}

bool Material::CreateNormalFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	m_HasNormal = CreateMapFromRGBA(2, w, h, p, m_NormalTexture, m_NormalUpload, m_NormalFootprint, m_NormalPending); return m_HasNormal;
}

bool Material::CreateMetalFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	m_HasMetal = CreateMapFromRGBA(3, w, h, p, m_MetalTexture, m_MetalUpload, m_MetalFootprint, m_MetalPending);  return m_HasMetal;
}

bool Material::CreateRoughFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	m_HasRough = CreateMapFromRGBA(4, w, h, p, m_RoughTexture, m_RoughUpload, m_RoughFootprint, m_RoughPending);  return m_HasRough;
}

bool Material::SetNormalTexture(const std::wstring& path)
{
	if (!EnsureSrvHeap()) return false;
	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (!LoadImgAny(path, metadata, image)) return false;
	m_HasNormal = UploadTextureTo(image.GetImage(0, 0, 0),metadata,2,
		m_NormalTexture,m_NormalUpload,m_NormalFootprint,m_NormalPending);
	return m_HasNormal;
}

bool Material::SetMetalTexture(const std::wstring& path)
{
	if (!EnsureSrvHeap()) return false;
	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (!LoadImgAny(path, metadata, image)) return false;
	m_HasMetal = UploadTextureTo(image.GetImage(0, 0, 0),metadata,3,
		m_MetalTexture,m_MetalUpload,m_MetalFootprint,m_MetalPending);
	return m_HasMetal;
}

bool Material::SetRoughTexture(const std::wstring& path)
{
	if (!EnsureSrvHeap()) return false;
	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (!LoadImgAny(path, metadata, image)) return false;
	m_HasRough = UploadTextureTo(image.GetImage(0, 0, 0),metadata,4,
		m_RoughTexture,m_RoughUpload,m_RoughFootprint,m_RoughPending);
	return m_HasRough;
}

bool Material::EnsureSrvHeap()
{
	if (m_TextureSrvHeap != nullptr) return true;

	auto device = APP->GetDevice();
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 7;                       // t0..t6
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_TextureSrvHeap.GetAddressOf()))))
		return false;

	// 全slotを1x1白のダミーSRVで初期化（未設定slotをサンプルしても安全に）
	D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
	nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullSrv.Texture2D.MipLevels = 1;
	const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < 5; ++i)
	{
		device->CreateShaderResourceView(nullptr, &nullSrv, cpu);  // null descriptor（サンプルすると0）
		cpu.ptr += inc;
	}
	return true;
}

bool Material::UploadTextureData(const DirectX::Image* srcImage, const DirectX::TexMetadata& metadata)
{
	// ----------------------- //
	// SRVヒープがなければ作成 //
	// ----------------------- //
	if (!EnsureSrvHeap()) return false;

	// ----------------------- //
	//   テクスチャ本体の作成  //
	// ----------------------- //
	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height),
		static_cast<UINT16>(metadata.arraySize),
		static_cast<UINT16>(metadata.mipLevels));

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(APP->GetDevice()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(m_Texture.GetAddressOf()))))
	{
		LOG->LogError("UploadTexture: CreateCommittedResource failed");
		return false;
	}

	// ----------------------------- //
	//   アップロードバッファの作成  //
	// ----------------------------- //
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0, uploadSize = 0;
	APP->GetDevice()->GetCopyableFootprints(
		&texDesc, 0, 1, 0, &m_TextureFootprint, &numRows, &rowSizeInBytes, &uploadSize);

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(APP->GetDevice()->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(m_TextureUpload.GetAddressOf()))))
	{
		LOG->LogError("UploadTexture: Create upload buffer failed");
		return false;
	}

	// ---------------- //
	// ピクセルをコピー //
	// ---------------- //
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (FAILED(m_TextureUpload->Map(0, &readRange, &mapped)))
	{
		LOG->LogError("UploadTexture: upload buffer map failed");
		return false;
	}
	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	const size_t copyBytes = (srcImage->rowPitch < static_cast<size_t>(rowSizeInBytes))
		? srcImage->rowPitch : static_cast<size_t>(rowSizeInBytes);
	for (UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * m_TextureFootprint.Footprint.RowPitch,
			srcImage->pixels + static_cast<size_t>(y) * srcImage->rowPitch,
			copyBytes);
	}
	m_TextureUpload->Unmap(0, nullptr);

	// --------- //
	// SRVの作成 //
	// --------- //
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
	APP->GetDevice()->CreateShaderResourceView(
		m_Texture.Get(), &srvDesc,
		m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart());

	m_TextureUploadPending = true;
	LOG->LogInfo("UploadTexture: texture loaded");
	return true;
}

bool Material::UploadTextureTo(const DirectX::Image* srcImage, const DirectX::TexMetadata& metadata, UINT srvSlot, ComPtr<ID3D12Resource>& outTexture, ComPtr<ID3D12Resource>& outUpload, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& outFootPrint, bool& outPending)
{
	auto device = APP->GetDevice();

	const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		static_cast<UINT64>(metadata.width),
		static_cast<UINT>(metadata.height),
		static_cast<UINT16>(metadata.arraySize),
		static_cast<UINT16>(metadata.mipLevels));

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(outTexture.ReleaseAndGetAddressOf())
	)))
	{
		return false;
	}

	// アップロードバッファ + ピクセルコピー
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0, uploadSize = 0;
	device->GetCopyableFootprints(
		&texDesc, 0, 1, 0, &outFootPrint, &numRows, &rowSizeInBytes, &uploadSize);

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(device->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(outUpload.ReleaseAndGetAddressOf())
	)))
	{
		return false;
	}

	// ピクセルをコピー
	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);	// 読み取りはしないので範囲は0
	if (FAILED(outUpload->Map(0, &readRange, &mapped)))
	{
		return false;
	}

	auto* dst = reinterpret_cast<std::uint8_t*>(mapped);
	const size_t copyBytes = (srcImage->rowPitch < static_cast<size_t>(rowSizeInBytes))
		? srcImage->rowPitch : static_cast<size_t>(rowSizeInBytes);
	for(UINT y = 0; y < metadata.height; ++y)
	{
		std::memcpy(
			dst + static_cast<size_t>(y) * outFootPrint.Footprint.RowPitch,
			srcImage->pixels + static_cast<size_t>(y) * srcImage->rowPitch,
			copyBytes);
	}
	outUpload->Unmap(0, nullptr);

	// SRVを指定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr = cpu.ptr + srvSlot * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	device->CreateShaderResourceView(outTexture.Get(), &srvDesc, cpu);

	outPending = true;
	return true;
}

// 既定の2段階ランプ(暗→明)を作って t1 に入れる
void Material::CreateDefaultRampTexture()
{
	if (m_TextureSrvHeap == nullptr) return;	// CreateCheckerTextureでヒープ作成済みの前提

	// 4x1ピクセル: 左半分=影色、右半分=明色
	constexpr UINT rampW = 4;
	static const std::uint8_t rampPixels[rampW * 4] = {
		 60,  60,  70, 255,
		 60,  60,  70, 255,
		255, 255, 255, 255,
		255, 255, 255, 255,
	};

	DirectX::Image img{};
	img.width = rampW;
	img.height = 1;
	img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	img.rowPitch = rampW * 4;
	img.slicePitch = img.rowPitch;
	img.pixels = const_cast<std::uint8_t*>(rampPixels);

	DirectX::TexMetadata meta{};
	meta.width = rampW;
	meta.height = 1;
	meta.depth = 1;
	meta.arraySize = 1;
	meta.mipLevels = 1;
	meta.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

	UploadTextureTo(&img, meta, 1,
		m_RampTexture, m_RampUpload, m_RampFootprint, m_RampUploadPending);
}

void Material::BindEnvironmentIfNeeded()
{
	if (m_EnvBound) return;
	if (!EnsureSrvHeap()) return;
	auto* env = APP->GetEnvTexture();
	if (!env || !m_TextureSrvHeap) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // HDRのフォーマットに合わせる
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = APP->GetEnvMipLevels();

	const UINT inc = APP->GetDevice()->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += 5 * inc;                       // slot5 = t5
	APP->GetDevice()->CreateShaderResourceView(env, &srv, cpu);

	m_EnvMaxMip = float(APP->GetEnvMipLevels() - 1);
	m_EnvBound = true;
}

void Material::BindShadowMapIfNeeded()
{
	if (m_ShadowBound) return;
	auto* sm = APP->GetShadowMap().GetResource();
	if (!sm || !m_TextureSrvHeap) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_FLOAT;      // TYPELESS深度をR32として読む
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;

	const UINT inc = APP->GetDevice()->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_TextureSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += 6 * inc;   // slot6 = t6
	APP->GetDevice()->CreateShaderResourceView(sm, &srv, cpu);
	m_ShadowBound = true;
}
