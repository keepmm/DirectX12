#include "Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdint>

#include <DirectXTex.h>

#include "Components.hpp"
#include "DirectX.hpp"
#include "Logger.hpp"
#include "Mesh.hpp"
#include "Util.hpp"
#include "Vertex.hpp"
#include "ModelLoader.hpp"

#undef min
#undef max

namespace fs = std::filesystem;

namespace
{
	bool LoadHeights(const std::string& path, int cols, int rows,
		std::vector<float>& out)
	{
		out.assign(static_cast<size_t>(cols) * rows, 0.0f);
		if (path.empty()) return false;

		const std::wstring wpath =
			fs::path(ResolveAssetPath(path)).wstring();

		DirectX::ScratchImage image;
		DirectX::TexMetadata meta{};

		HRESULT hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, &meta, image);

		if (FAILED(hr))
		{
			hr = DirectX::LoadFromTGAFile(wpath.c_str(), &meta, image);
		}

		if (FAILED(hr))
		{
			LOG->LogWarning("Terrain : ハイトマップを読めません" + path);
			return false;
		}

		// 形式がまちまちなので 8bit RGBAにそろえて読む
		DirectX::ScratchImage rgba;
		if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			if (FAILED(DirectX::Convert(*image.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM,
				DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
				rgba)))
			{
				LOG->LogWarning("Terrain : ハイトマップの形式を変換できません: " + path);
				return false;
			}
		}
		else
		{
			rgba = std::move(image);
		}

		const DirectX::Image* img = rgba.GetImage(0, 0, 0);
		if (!img || img->width == 0 || img->height == 0)return false;

		// 画像の大きさと格子の数は違ってよい
		for (int z = 0; z < rows; ++z)
		{
			const size_t sy = static_cast<size_t>((rows > 1) ? (double)z / (rows - 1) * (img->height - 1) : 0);

			const uint8_t* line = img->pixels + sy * img->rowPitch;
			for (int x = 0; x < cols; ++x)
			{
				const size_t sx = static_cast<size_t>(
						(cols > 1) ? (double)x / (cols - 1) * (img->width - 1) : 0);

				out[static_cast<size_t>(z) * cols + x] = line[sx * 4] / 255.0f;
			}
		}
		return true;
	}

	/// @brief 彫った高さ(.r16)を読む。無ければ false
	bool LoadRaw16(const std::string& path, int cols, int rows, std::vector<float>& out)
	{
		if (path.empty()) return false;

		const fs::path file = ResolveAssetPath(path);
		std::error_code ec;
		const auto bytes = fs::file_size(file, ec);
		if (ec) return false;

		const size_t need = static_cast<size_t>(cols) * rows * sizeof(std::uint16_t);
		if (bytes != need)
		{
			// 分割数を変えると大きさが合わなくなる。そのときは画像から作り直す
			LOG->LogWarning("Terrain: 高さデータの大きさが合いません(分割数を変えた?): " + path);
			return false;
		}

		std::ifstream in(file, std::ios::binary);
		if (!in) return false;

		std::vector<std::uint16_t> raw(static_cast<size_t>(cols) * rows);
		in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(need));

		out.resize(raw.size());
		for (size_t i = 0; i < raw.size(); ++i) out[i] = raw[i] / 65535.0f;
		return true;
	}
}

void Terrain::Rebuild(World& world, Entity entity)
{
	if (!world.HasComponent<TerrainComponent>(entity)) return;
	auto& t = world.GetComponent<TerrainComponent>(entity);

	t.gridX = std::clamp(t.gridX,2,512);
	t.gridZ = std::clamp(t.gridZ,2,512);

	const int cols = t.gridX + 1;
	const int rows = t.gridZ + 1;

	// 彫ったデータがあればそれを使う。無ければ画像から
	if (!LoadRaw16(t.dataPath, cols, rows, t.Heights))
	{
		LoadHeights(t.HeightMapPath, cols, rows, t.Heights);
	}

	++t.heightsVersion;

	BuildMesh(world, entity);
}

void Terrain::BuildMesh(World& world, Entity entity)
{
	if (!world.HasComponent<TerrainComponent>(entity)) return;
	auto& t = world.GetComponent<TerrainComponent>(entity);

	const int cols = t.gridX + 1;
	const int rows = t.gridZ + 1;
	if (t.Heights.size() != static_cast<size_t>(cols) * rows) return;

	// ---- 頂点 ---- //
	std::vector<Vertex> vertices(static_cast<size_t>(cols) * rows);
	const float dx = t.Width / t.gridX;
	const float dz = t.Depth / t.gridZ;

	for (int z = 0; z < rows; ++z)
	{
		for (int x = 0; x < cols; ++x)
		{
			const size_t i = static_cast<size_t>(z) * cols + x;
			Vertex& v = vertices[i];

			v.position =
			{
				-t.Width* 0.5f + x * dx,
				t.Heights[i] * t.Height,
				-t.Depth * 0.5f + z * dz
			};

			v.col = { 1.0f,1.0f,1.0f,1.0f };
			v.uv = { (float)x / t.gridX * t.uvTiling,(float)z / t.gridZ * t.uvTiling };
		}
	}

	// ---- 法線(隣との高さの差を出す) ---- //
	for (int z = 0; z < rows; ++z)
	{
		for (int x = 0; x < cols; ++x)
		{
			const size_t i = static_cast<size_t>(z) * cols + x;
			auto h = [&](int ix, int iz)
				{
					ix = std::clamp(ix, 0, cols - 1);
					iz = std::clamp(iz, 0, rows - 1);
					return t.Heights[(size_t)iz * cols + ix] * t.Height;
				};

			const float hl = h(x - 1, z);
			const float hr = h(x + 1, z);
			const float hd = h(x, z - 1);
			const float hu = h(x, z + 1);

			DirectX::XMFLOAT3 n =
			{
				hl - hr,
				2.0f * dx,
				hd - hu
			};

			DirectX::XMStoreFloat3(&n, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&n)));
			vertices[i].normal = n;
			vertices[i].tangent = { 1.0f,0.0f,0.0f };
		}
	}

	// ---- インデックス ---- //
	std::vector<std::uint32_t> indices;
	indices.reserve(static_cast<size_t>(t.gridX) * t.gridZ * 6);
	for (int z = 0; z < t.gridZ; ++z)
	{
		for (int x = 0; x < t.gridX; ++x)
		{
			const std::uint32_t i0 = static_cast<std::uint32_t>(z * cols + x);
			const std::uint32_t i1 = i0 + 1;
			const std::uint32_t i2 = i0 + cols;
			const std::uint32_t i3 = i2 + 1;

			indices.insert(indices.end(), { i0,i2,i1 });
			indices.insert(indices.end(), { i1,i2,i3 });
		}
	}

	// ---- メッシュに格納 ---- //
	auto mesh = std::make_shared<Mesh>();
	mesh->Init(APP->GetDevice(), vertices, indices, nullptr);

	APP->WaitForGPUIdle();

	MeshComponent mc{};
	mc.mesh = mesh;
	mc.FilePath.clear();	// ファイル由来ではないので読み込みではTerrainComponentから作り直す
	if (world.HasComponent<MeshComponent>(entity)) world.GetComponent<MeshComponent>(entity) = mc;
	else world.AddComponent<MeshComponent>(entity, mc);

	// マテリアル: 無ければ作り、あってもスロット0を埋める
	// (シーン読込では Material が先に載るので、ここで materials が空のまま残る)
	MaterialComponent& mat = world.HasComponent<MaterialComponent>(entity)
		? world.GetComponent<MaterialComponent>(entity)
		: world.AddComponent<MaterialComponent>(entity, MaterialComponent{});

	if (!mat.material)
	{
		mat.shaderName = "PBR";
		mat.material = std::make_shared<Material>();
		mat.material->Init();
		mat.material->baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	}
	if (mat.materials.empty())
	{
		mat.materials.push_back(mat.material);
		mat.materialnames.push_back("Terrain");
	}
	mat.materialAssets.resize(mat.materials.size());

	// シーンに保存されていた値と .mat の割り当てを適用する
	// (これをしないと、開き直すたびに白へ戻る)
	ModelLoader::ApplyPendingSubMaterials(mat);

	t.BuiltSettings = HashSettings(t);
	LOG->LogInfo("Terrain : 生成 " + std::to_string(cols) + "x" + std::to_string(rows) +
		" 頂点=" + std::to_string(vertices.size()));
}

size_t Terrain::HashSettings(const TerrainComponent& terrain)
{
	size_t h = 1469598103934665603ull;
	auto mix = [&h](const void* p, size_t n)
		{
			const auto* b = static_cast<const unsigned char*>(p);
			for (size_t i = 0; i < n; ++i)
			{
				h ^= b[i];
				h *= 1099511628211ull;
			}
		};

	mix(terrain.HeightMapPath.data(),terrain.HeightMapPath.size());
	mix(&terrain.Width, sizeof(terrain.Width));
	mix(&terrain.Depth, sizeof(terrain.Depth));
	mix(&terrain.Height, sizeof(terrain.Height));
	mix(&terrain.gridX, sizeof(terrain.gridX));
	mix(&terrain.gridZ, sizeof(terrain.gridZ));
	mix(&terrain.uvTiling, sizeof(terrain.uvTiling));
	return h;
}

float Terrain::SampleHeight(const TerrainComponent& terrain, float x, float z)
{
	const int cols = terrain.gridX + 1;
	const int rows = terrain.gridZ + 1;
	if (terrain.Heights.size() != static_cast<size_t>(cols) * rows) return 0.0f;

	// 中心が原点
	const float u = std::clamp((x + terrain.Width * 0.5f) / terrain.Width, 0.0f,1.0f);
	const float v = std::clamp((z + terrain.Depth * 0.5f) / terrain.Depth,0.0f,1.0f);

	const float fx = u * (cols - 1);
	const float fz = v * (rows - 1);
	const int x0 = (int)fx;
	const int z0 = (int)fz;
	const int x1 = (std::min(x0 + 1, cols - 1));
	const int z1 = (std::min(z0 + 1, rows - 1));
	const float tx = fx - x0;
	const float tz = fz - z0;

	auto at = [&](int ix, int iz)
		{
			return terrain.Heights[(size_t)iz * cols + ix];
		};

	// 双一次補完
	const float a = at(x0, z0) * (1 - tx) + at(x1, z0) * tx;
	const float b = at(x0, z1) * (1 - tx) + at(x1, z1) * tx;
	return (a * (1 - tz) + b * tz) * terrain.Height;
}

bool Terrain::SaveHeights(const TerrainComponent& t, const std::string& path)
{
	const fs::path file = path;
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);

	std::ofstream out{ file, std::ios::binary };
	if (!out)
	{
		LOG->LogWarning("Terrain: 高さデータを書けません: " + path);
		return false;
	}

	std::vector<std::uint16_t> raw(t.Heights.size());
	for (size_t i = 0; i < t.Heights.size(); ++i)
		raw[i] = static_cast<std::uint16_t>(std::clamp(t.Heights[i], 0.0f, 1.0f) * 65535.0f);

	out.write(reinterpret_cast<const char*>(raw.data()),
		static_cast<std::streamsize>(raw.size() * sizeof(std::uint16_t)));

	LOG->LogInfo("Terrain: 高さを保存 " + path);
	return true;
}

bool Terrain::Raycast(
	const TerrainComponent& t, const float4x4& terrainWorld,
	const float3& rayOrigin, const float3& rayDir, float maxDistance, float3& outLocalHit)
{
	using namespace DirectX;
	if (t.Heights.empty()) return false;

	// 地形のローカル空間へ持ち込む(動かしていても回していても効く)
	XMVECTOR det;
	const XMMATRIX inv = XMMatrixInverse(&det, XMLoadFloat4x4(&terrainWorld));
	if (XMVectorGetX(XMVectorEqual(det, XMVectorZero()))) return false;

	const XMVECTOR o = XMVector3TransformCoord(XMLoadFloat3(&rayOrigin), inv);
	const XMVECTOR d = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&rayDir), inv));

	// 格子1マスぶんずつ進めて、地面より下に潜った区間を挟み込む
	const float step = (std::min)(t.Width / t.gridX, t.Depth / t.gridZ);
	float prevT = 0.0f;
	float prevDiff = 0.0f;
	bool hasPrev = false;

	for (float s = 0.0f; s <= maxDistance; s += step)
	{
		XMFLOAT3 p; XMStoreFloat3(&p, XMVectorAdd(o, XMVectorScale(d, s)));
		const float diff = p.y - SampleHeight(t, p.x, p.z);

		if (hasPrev && prevDiff > 0.0f && diff <= 0.0f)
		{
			// 二分探索で交点を詰める
			float lo = prevT, hi = s;
			for (int i = 0; i < 12; ++i)
			{
				const float mid = (lo + hi) * 0.5f;
				XMFLOAT3 m; XMStoreFloat3(&m, XMVectorAdd(o, XMVectorScale(d, mid)));
				if (m.y - SampleHeight(t, m.x, m.z) > 0.0f) lo = mid;
				else                                        hi = mid;
			}
			XMStoreFloat3(&outLocalHit, XMVectorAdd(o, XMVectorScale(d, (lo + hi) * 0.5f)));

			// 地形の外は当たりにしない
			if (std::fabs(outLocalHit.x) > t.Width * 0.5f ||
				std::fabs(outLocalHit.z) > t.Depth * 0.5f) return false;
			return true;
		}
		prevT = s; prevDiff = diff; hasPrev = true;
	}
	return false;
}

void Terrain::ApplyBrush(
	TerrainComponent& t, const float3& center, const Brush& brush, float deltaTime)
{
	const int cols = t.gridX + 1;
	const int rows = t.gridZ + 1;
	if (t.Heights.size() != static_cast<size_t>(cols) * rows) return;
	if (t.Height <= 0.0f) return;

	const float dx = t.Width / t.gridX;
	const float dz = t.Depth / t.gridZ;

	// 半径に入る格子だけを回す(全体を舐めると 512 分割で重い)
	const int cx = static_cast<int>((center.x + t.Width * 0.5f) / dx);
	const int cz = static_cast<int>((center.z + t.Depth * 0.5f) / dz);
	const int rx = static_cast<int>(brush.radius / dx) + 1;
	const int rz = static_cast<int>(brush.radius / dz) + 1;

	// 高さは 0〜1 で持っているので、ワールド単位の変化量を割り戻す
	const float amount = brush.strength * deltaTime / t.Height;
	const float target = std::clamp(brush.targetHeight / t.Height, 0.0f, 1.0f);

	const std::vector<float> before = t.Heights;   // ならし用に元の値を残す

	for (int z = (std::max)(0, cz - rz); z <= (std::min)(rows - 1, cz + rz); ++z)
	{
		for (int x = (std::max)(0, cx - rx); x <= (std::min)(cols - 1, cx + rx); ++x)
		{
			const float wx = -t.Width * 0.5f + x * dx;
			const float wz = -t.Depth * 0.5f + z * dz;
			const float dist = std::sqrt((wx - center.x) * (wx - center.x) +
				(wz - center.z) * (wz - center.z));
			if (dist > brush.radius) continue;

			// 中心ほど強く、縁でなめらかに 0 になる
			const float n = dist / brush.radius;
			const float falloff = 1.0f - n * n * (3.0f - 2.0f * n);

			const size_t i = static_cast<size_t>(z) * cols + x;
			float& h = t.Heights[i];

			switch (brush.mode)
			{
			case BrushMode::Raise: h += amount * falloff; break;
			case BrushMode::Lower: h -= amount * falloff; break;
			case BrushMode::Flatten:
				h += (target - h) * (std::min)(1.0f, amount * falloff * 4.0f);
				break;
			case BrushMode::Smooth:
			{
				// 周り8マスの平均へ寄せる
				float sum = 0.0f; int n8 = 0;
				for (int oz = -1; oz <= 1; ++oz)
				{
					for (int ox = -1; ox <= 1; ++ox)
					{
						const int sx = std::clamp(x + ox, 0, cols - 1);
						const int sz = std::clamp(z + oz, 0, rows - 1);
						sum += before[static_cast<size_t>(sz) * cols + sx];
						++n8;
					}
				}
				const float avg = sum / n8;
				h += (avg - h) * (std::min)(1.0f, amount * falloff * 4.0f);
				break;
			}
			}
			h = std::clamp(h, 0.0f, 1.0f);
		}
	}

	++t.heightsVersion;
}
