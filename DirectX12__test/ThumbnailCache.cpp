/*!*************************************************************
 * \file   ThumbnailCache.cpp
 * \brief  アセットブラウザ用のサムネイル(モデル / マテリアル / 画像)
 *
 * 作成者 keeep
 * 作成日 2026/9/14
 * 更新履歴	9.14 作成
 * *********************************************************************/
#include "ThumbnailCache.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <DirectXTex.h>

#include "AssetDatabase.hpp"
#include "DirectX.hpp"
#include "IconLibrary.hpp"
#include "Logger.hpp"
#include "Material.hpp"
#include "MaterialLibrary.hpp"
#include "MaterialPreview.hpp"
#include "Mesh.hpp"
#include "ModelData.hpp"
#include "ModelLoader.hpp"
#include "Project.hpp"
#include "TextureLoader.hpp"
#include "Util.hpp"

namespace fs = std::filesystem;

namespace
{
	constexpr UINT kTextureThumbSize = 128;

	bool IsModelExt(const std::string& e)
	{
		return e == ".fbx" || e == ".pmx" || e == ".pmd" || e == ".gltf" ||
			e == ".glb" || e == ".obj" || e == ".dae" || e == ".x";
	}

	bool IsTextureExt(const std::string& e)
	{
		// .png / .jpg / .bmp は IconLibrary が画像そのものを読めるので対象外
		return e == ".tga" || e == ".dds" || e == ".hdr" || e == ".jpeg";
	}

	std::string LowerExt(const fs::path& p)
	{
		std::string e = p.extension().string();
		std::transform(e.begin(), e.end(), e.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return e;
	}

	/// @brief 画像を読み、長辺 kTextureThumbSize に縮めて PNG に保存する(別スレッドで呼ぶ)
	bool MakeTextureThumbnail(const std::wstring& src, const std::wstring& out)
	{
		// WIC は COM を使うので、このスレッドでも初期化が要る
		const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		bool ok = false;
		do
		{
			DirectX::TexMetadata meta{};
			DirectX::ScratchImage img;
			if (FAILED(TextureLoader::LoadAny(src, meta, img))) break;

			// DDS の圧縮は展開、HDR / float は 8bit に落とす
			if (DirectX::IsCompressed(meta.format))
			{
				DirectX::ScratchImage d;
				if (FAILED(DirectX::Decompress(*img.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, d))) break;
				img = std::move(d);
			}
			else if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM)
			{
				DirectX::ScratchImage c;
				if (FAILED(DirectX::Convert(*img.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM,
					DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, c))) break;
				img = std::move(c);
			}

			const DirectX::Image& s = *img.GetImage(0, 0, 0);
			const float k = float(kTextureThumbSize) / float((std::max)(s.width, s.height));
			const size_t w = (std::max)(size_t(1), size_t(s.width * k));
			const size_t h = (std::max)(size_t(1), size_t(s.height * k));

			DirectX::ScratchImage r;
			if (FAILED(DirectX::Resize(s, w, h, DirectX::TEX_FILTER_DEFAULT, r))) break;

			ok = SUCCEEDED(DirectX::SaveToWICFile(*r.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
				DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), out.c_str()));
		} while (false);

		if (SUCCEEDED(co)) CoUninitialize();
		return ok;
	}
}

ThumbnailCache& ThumbnailCache::Get()
{
	static ThumbnailCache instance;
	return instance;
}

bool ThumbnailCache::Supports(const std::string& extLower)
{
	return IsModelExt(extLower) || IsTextureExt(extLower) || extLower == ".mat";
}

std::wstring ThumbnailCache::CachePathOf(const std::string& assetPath) const
{
	if (!PROJECT->IsOpen()) return {};

	const fs::path p = Utf8ToPath(assetPath);
	std::error_code ec;
	const auto stamp = fs::last_write_time(p, ec);
	if (ec) return {};

	// GUID が取れればそれ(リネームしても作り直さない)、取れなければパスのハッシュ
	std::string id;
	try { id = ASSETDB->PathToGuid(p.generic_string()); }
	catch (...) {}	// CP932 に無い文字のパスは generic_string が投げる
	if (id.empty())
	{
		id = std::to_string(std::hash<std::wstring>{}(p.generic_wstring()));
	}

	char buf[32] = {};
	std::snprintf(buf, sizeof(buf), "_%llx",
		static_cast<unsigned long long>(stamp.time_since_epoch().count()));

	return (PROJECT->GetLibraryDir() / "Thumbnails" / (id + buf + ".png")).wstring();
}

ImTextureID ThumbnailCache::Request(const std::string& assetPath)
{
	const std::wstring cache = CachePathOf(assetPath);
	if (cache.empty() || m_Failed.count(cache) != 0) return 0;

	std::error_code ec;
	if (fs::exists(cache, ec))
	{
		// IconLibrary は 1 フレーム 1 枚ずつ読むので、並んでいても順に出てくる
		return IconLibrary::Get()->GetOrLoad(cache);
	}

	if (m_Queued.insert(cache).second)
	{
		const std::string ext = LowerExt(Utf8ToPath(assetPath));
		Job job;
		job.assetPath = assetPath;
		job.cachePath = cache;
		job.kind = IsModelExt(ext) ? Kind::Model
			: (ext == ".mat") ? Kind::Material : Kind::Texture;
		m_Queue.push_back(std::move(job));
	}
	return 0;
}

// ------------------------------------------------------------------ //
//                             進行                                    //
// ------------------------------------------------------------------ //

void ThumbnailCache::Update()
{
	auto ready = [](const auto& f)
		{
			return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		};

	switch (m_Stage)
	{
	case Stage::Idle:
		StartNext();
		break;

	case Stage::Loading:
		if (m_Current.kind == Kind::Texture && ready(m_TextureTask))
			Finish(m_TextureTask.get());
		else if (m_Current.kind == Kind::Model && ready(m_ModelTask))
			FinishModelLoad();
		break;

	case Stage::WaitCapture:
		// 描いたコマンドリストが GPU で実行し終わるまで待つ
		if (--m_CaptureWait <= 0) Capture();
		break;

	default:
		break;
	}
}

void ThumbnailCache::StartNext()
{
	while (!m_Queue.empty())
	{
		m_Current = std::move(m_Queue.front());
		m_Queue.pop_front();

		std::error_code ec;
		if (fs::exists(m_Current.cachePath, ec)) { m_Queued.erase(m_Current.cachePath); continue; }

		fs::create_directories(fs::path(m_Current.cachePath).parent_path(), ec);

		try
		{
			switch (m_Current.kind)
			{
			case Kind::Texture:
			{
				const std::wstring src = Utf8ToPath(m_Current.assetPath).wstring();
				const std::wstring out = m_Current.cachePath;
				m_TextureTask = std::async(std::launch::async,
					[src, out]() { return MakeTextureThumbnail(src, out); });
				m_Stage = Stage::Loading;
				return;
			}
			case Kind::Model:
			{
				// ModelLoader は engine 流儀のパス(generic_string)を受ける
				const std::string path = Utf8ToPath(m_Current.assetPath).generic_string();
				m_ModelTask = std::async(std::launch::async,
					[path]() { return std::make_shared<ModelCpuData>(ModelLoader::ParseFile(path, 1.0f)); });
				m_Stage = Stage::Loading;
				return;
			}
			case Kind::Material:
			{
				m_Material = MaterialLibrary::Get().Load(Utf8ToPath(m_Current.assetPath).generic_string());
				if (!m_Material) { Finish(false); continue; }
				m_Center = { 0.0f, 0.0f, 0.0f };
				m_Radius = 1.0f;
				m_Stage = Stage::WaitRender;
				return;
			}
			}
		}
		catch (...)
		{
			// CP932 に無い文字のパスなど。作れないものとして記録して次へ
			Finish(false);
		}
	}
}

void ThumbnailCache::FinishModelLoad()
{
	std::shared_ptr<ModelCpuData> cpu;
	try { cpu = m_ModelTask.get(); }
	catch (...) {}

	if (!cpu || cpu->vertices.empty())
	{
		Finish(false);
		return;
	}

	// 全頂点の外接箱 → 外接球。カメラをこれが収まる距離に置く
	float3 mn = cpu->vertices[0].position;
	float3 mx = mn;
	for (const auto& v : cpu->vertices)
	{
		mn = { (std::min)(mn.x, v.position.x), (std::min)(mn.y, v.position.y), (std::min)(mn.z, v.position.z) };
		mx = { (std::max)(mx.x, v.position.x), (std::max)(mx.y, v.position.y), (std::max)(mx.z, v.position.z) };
	}
	m_Center = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
	const float dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
	m_Radius = (std::max)(0.001f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));

	// GPU への転送とマテリアル作成はメインスレッドで
	ModelLoadResult result = ModelLoader::upload(*cpu);
	m_Mesh = result.mesh;
	m_Materials = BuildMaterials(result, "PBR");

	if (!m_Mesh) { Finish(false); return; }
	m_Stage = Stage::WaitRender;
}

void ThumbnailCache::Render(ID3D12GraphicsCommandList* cmd, UINT frameIndex)
{
	if (m_Stage != Stage::WaitRender || cmd == nullptr) return;

	auto& preview = MaterialPreview::Get();
	if (!preview.IsReady()) return;

	// 深度バッファを MaterialPreview と共有するので、同じサイズで作る
	if (!m_Target.IsValid() && FAILED(m_Target.Init(preview.Size(), preview.Size())))
	{
		Finish(false);
		return;
	}

	// ---- 描画先の準備(MaterialPreview::RenderOne と同じ手順) ---- //
	m_Target.SetResourceBarrier(cmd,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_Target.GetRTV();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = preview.DepthHandle();
	cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	m_Target.Clear(cmd, { 0.16f, 0.17f, 0.19f, 1.0f });
	cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT vp = m_Target.GetViewport();
	D3D12_RECT sc = m_Target.GetScissorRect();
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->SetGraphicsRootSignature(APP->GetRootSignature().Get());

	// ---- カメラ: 外接球が画面に収まる距離から、斜め上手前を見る ---- //
	using namespace DirectX;
	const float fov = XMConvertToRadians(35.0f);
	const float dist = m_Radius / std::sin(fov * 0.5f) * 1.05f;
	const XMVECTOR center = XMLoadFloat3(&m_Center);
	const XMVECTOR dir = XMVector3Normalize(XMVectorSet(0.55f, 0.35f, -1.0f, 0.0f));   // 左手系の手前
	const XMVECTOR eye = XMVectorAdd(center, XMVectorScale(dir, dist));

	float4x4 view{}, proj{}, world{};
	XMStoreFloat4x4(&view, XMMatrixLookAtLH(eye, center, XMVectorSet(0, 1, 0, 0)));
	XMStoreFloat4x4(&proj, XMMatrixPerspectiveFovLH(fov, 1.0f, dist * 0.01f, dist + m_Radius * 2.0f));
	XMStoreFloat4x4(&world, XMMatrixIdentity());

	MaterialPreview::BindPreviewLight(cmd, frameIndex);
	auto& cb = APP->GetConstantBufferAllocator();

	// ---- 描く ---- //
	if (m_Current.kind == Kind::Material)
	{
		m_Material->Apply(cmd, world, view, proj, false, frameIndex, &cb, m_Material->shaderName);
		preview.Sphere().Draw(cmd);
	}
	else
	{
		// ボーンの無いバインドポーズで描く(頂点の形式は静的メッシュと共通なので PBR でそのまま描ける)
		const UINT subCount = m_Mesh->GetSubMeshCount();
		const UINT passes = (std::max)(subCount, UINT(1));
		for (UINT s = 0; s < passes; ++s)
		{
			UINT mi = (subCount > 0) ? m_Mesh->GetSubMeshMaterialIndex(s) : 0;
			if (mi >= m_Materials.size()) mi = 0;
			if (m_Materials.empty() || !m_Materials[mi]) continue;

			m_Materials[mi]->Apply(cmd, world, view, proj, false, frameIndex, &cb, "PBR");
			if (subCount > 0) m_Mesh->DrawSubMesh(cmd, s);
			else              m_Mesh->Draw(cmd);
		}
	}

	m_Target.SetResourceBarrier(cmd,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	m_Stage = Stage::WaitCapture;
	m_CaptureWait = RTV_NUM + 1;
}

void ThumbnailCache::Capture()
{
	// 描いたフレームまで GPU を追いつかせてから読み戻す
	APP->WaitForGPUIdle();

	DirectX::ScratchImage captured;
	const HRESULT hr = DirectX::CaptureTexture(
		APP->GetCommandQueue().Get(), m_Target.GetResource().Get(), false, captured,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	bool ok = false;
	if (SUCCEEDED(hr))
	{
		ok = SUCCEEDED(DirectX::SaveToWICFile(*captured.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), m_Current.cachePath.c_str()));
	}

	// GPU は待ち終わっているので、ここで手放してよい
	m_Mesh.reset();
	m_Materials.clear();
	m_Material.reset();

	Finish(ok);
}

void ThumbnailCache::Finish(bool ok)
{
	if (!ok)
	{
		m_Failed.insert(m_Current.cachePath);
		LOG->LogWarning("サムネイルを作れませんでした: " + m_Current.assetPath);
	}
	m_Queued.erase(m_Current.cachePath);
	m_Current = Job{};
	m_Stage = Stage::Idle;
}

void ThumbnailCache::Release()
{
	if (m_ModelTask.valid()) m_ModelTask.wait();
	if (m_TextureTask.valid()) m_TextureTask.wait();

	m_Mesh.reset();
	m_Materials.clear();
	m_Material.reset();
	if (m_Target.IsValid()) m_Target.Release();

	m_Queue.clear();
	m_Queued.clear();
	m_Stage = Stage::Idle;
}
