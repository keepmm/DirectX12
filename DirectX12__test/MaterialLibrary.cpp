/*!*************************************************************
 * \file   MaterialLibrary.cpp
 * \brief  マテリアルアセット(.mat)の読み書きと共有
 *
 * 作成者 keeep
 * 作成日 2026/9/14
 * 更新履歴	9.14 作成
 * *********************************************************************/
#include "MaterialLibrary.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "AssetDatabase.hpp"
#include "Logger.hpp"
#include "Material.hpp"
#include "Project.hpp"
#include "Util.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	constexpr int kMatVersion = 1;

	/// @brief .mat の "textures" のキーと SRV スロットの対応
	struct SlotKey
	{
		UINT slot;
		const char* key;
		bool srgb;	// ベースカラーと発光は sRGB として読む(モデル読み込み時と揃える)
	};
	constexpr SlotKey kSlots[] =
	{
		{ TexSlot::Albedo,    "albedo",    true  },
		{ TexSlot::Normal,    "normal",    false },
		{ TexSlot::Metal,     "metal",     false },
		{ TexSlot::Rough,     "rough",     false },
		{ TexSlot::Emissive,  "emissive",  true  },
		{ TexSlot::Occlusion, "occlusion", false },
		{ TexSlot::Ramp,      "ramp",      false },
	};

	json ColorToJson(const float4& c) { return { c.x, c.y, c.z, c.w }; }

	float4 JsonToColor(const json& j, const float4& def)
	{
		if (!j.is_array() || j.size() < 3) return def;
		return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>(),
			j.size() > 3 ? j[3].get<float>() : 1.0f };
	}

	json ToJson(const Material& m, const std::string& shader)
	{
		json j;
		j["matVersion"] = kMatVersion;
		j["shader"] = shader;

		json& p = j["params"];
		p["baseColor"] = ColorToJson(m.baseColor);
		p["baseAlpha"] = m.baseAlpha;
		p["roughness"] = m.roughness;
		p["metallic"] = m.metallic;
		p["rimColor"] = ColorToJson(m.rimColor);
		p["emissiveColor"] = ColorToJson(m.emissiveColor);
		p["emissiveStrength"] = m.emissiveStrength;
		p["reflectStrength"] = m.reflectStrength;
		p["reflectFade"] = m.reflectFade;
		p["reflectBlur"] = m.reflectBlur;
		p["sssStrength"] = m.sssStrength;
		p["sssWrap"] = m.sssWrap;
		p["sssTrans"] = m.sssTrans;
		p["sssColor"] = ColorToJson(m.sssColor);
		p["sheen"] = m.sheen;
		p["isFace"] = m.isFace;
		p["outlineWidth"] = m.outlineWidth;
		p["waveHeight"] = m.waveHeight;
		p["waveLength"] = m.waveLength;
		p["waveSpeed"] = m.waveSpeed;
		p["waterGloss"] = m.waterGloss;
		p["waterOpacity"] = m.waterOpacity;
		p["waterTurbidity"] = m.waterTurbidity;
		p["waterFoam"] = m.waterFoam;
		p["waveAmplitude"] = m.waveAmplitude;
		p["waveSteepness"] = m.waveSteepness;

		json& t = j["textures"] = json::object();
		for (const auto& s : kSlots)
		{
			const std::string& src = m.Textures().SourcePath(s.slot);
			if (!src.empty())
			{
				t[s.key] = AssetRefToJson(src);
			}
			else if (s.slot != TexSlot::Ramp && m.Textures().Valid(s.slot))
			{
				// 貼られているのに元ファイルが無い = モデルの埋め込みテクスチャ
				LOG->LogWarning(std::string("埋め込みテクスチャは .mat に含められません: ") + s.key);
			}
		}
		return j;
	}

	void ApplyJson(Material& m, const json& j)
	{
		m.shaderName = j.value("shader", std::string("PBR"));

		if (j.contains("params"))
		{
			const json& p = j["params"];
			m.baseColor = JsonToColor(p.value("baseColor", json()), m.baseColor);
			m.baseAlpha = p.value("baseAlpha", m.baseAlpha);
			m.roughness = p.value("roughness", m.roughness);
			m.metallic = p.value("metallic", m.metallic);
			m.rimColor = JsonToColor(p.value("rimColor", json()), m.rimColor);
			m.emissiveColor = JsonToColor(p.value("emissiveColor", json()), m.emissiveColor);
			m.emissiveStrength = p.value("emissiveStrength", m.emissiveStrength);
			m.reflectStrength = p.value("reflectStrength", m.reflectStrength);
			m.reflectFade = p.value("reflectFade", m.reflectFade);
			m.reflectBlur = p.value("reflectBlur", m.reflectBlur);
			m.sssStrength = p.value("sssStrength", m.sssStrength);
			m.sssWrap = p.value("sssWrap", m.sssWrap);
			m.sssTrans = p.value("sssTrans", m.sssTrans);
			m.sssColor = JsonToColor(p.value("sssColor", json()), m.sssColor);
			m.sheen = p.value("sheen", m.sheen);
			m.isFace = p.value("isFace", m.isFace);
			m.outlineWidth = p.value("outlineWidth", m.outlineWidth);
			m.waveHeight = p.value("waveHeight", m.waveHeight);
			m.waveLength = p.value("waveLength", m.waveLength);
			m.waveSpeed = p.value("waveSpeed", m.waveSpeed);
			m.waterGloss = p.value("waterGloss", m.waterGloss);
			m.waterOpacity = p.value("waterOpacity", m.waterOpacity);
			m.waterTurbidity = p.value("waterTurbidity", m.waterTurbidity);
			m.waterFoam = p.value("waterFoam", m.waterFoam);
			m.waveAmplitude = p.value("waveAmplitude", m.waveAmplitude);
			m.waveSteepness = p.value("waveSteepness", m.waveSteepness);
		}

		if (!j.contains("textures")) return;
		const json& t = j["textures"];
		for (const auto& s : kSlots)
		{
			if (!t.contains(s.key)) continue;
			const std::string path = AssetRefFromJson(t[s.key]);
			if (path.empty()) continue;

			const std::wstring w = fs::path(ResolveAssetPath(path)).wstring();
			const bool ok = (s.slot == TexSlot::Ramp)
				? m.SetToonRampTexture(w)
				: m.Textures().LoadFromFile(s.slot, w, s.srgb);

			if (ok) m.Textures().SetSourcePath(s.slot, path);
			else    LOG->LogWarning("テクスチャを読み込めません(Missing): " + path);
		}
	}

	bool WriteJson(const std::string& path, const json& j)
	{
		std::ofstream out{ fs::path(path) };   // () だと関数宣言と解釈される
		if (!out)
		{
			LOG->LogError("マテリアルを書き込めません: " + path);
			return false;
		}
		out << std::setw(2) << j;
		return true;
	}

	/// @brief ファイル名に使えない文字を置き換える
	std::string SanitizeFileName(std::string s)
	{
		for (char& c : s)
		{
			if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) c = '_';
		}
		return s.empty() ? std::string("NewMaterial") : s;
	}

	/// @brief "Foo.mat" が既にあれば "Foo 1.mat", "Foo 2.mat" ...
	fs::path UniquePath(const std::string& dir, const std::string& utf8Name)
	{
		fs::path file = fs::path(dir) / Utf8ToPath(utf8Name + ".mat");
		for (int n = 1; fs::exists(file); ++n)
		{
			file = fs::path(dir) / Utf8ToPath(utf8Name + " " + std::to_string(n) + ".mat");
		}
		return file;
	}
}

MaterialLibrary& MaterialLibrary::Get()
{
	static MaterialLibrary instance;
	return instance;
}

bool MaterialLibrary::IsMaterialPath(const std::string& path)
{
	if (path.size() < 4) return false;
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".mat";
}

std::string MaterialLibrary::KeyOf(const std::string& path) const
{
	if (ASSETDB->IsReady())
	{
		const AssetGuid guid = ASSETDB->EnsureGuid(path);
		if (!guid.empty()) return guid;
	}
	// ゲーム版(AssetDatabase 未起動)はパスで引く
	std::string key = fs::path(path).lexically_normal().generic_string();
	std::transform(key.begin(), key.end(), key.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return key;
}

std::shared_ptr<Material> MaterialLibrary::Load(const std::string& path)
{
	if (path.empty()) return nullptr;

	const std::string key = KeyOf(path);
	if (auto it = m_Cache.find(key); it != m_Cache.end()) return it->second;

	std::ifstream in(fs::path(ResolveAssetPath(path)));
	if (!in)
	{
		LOG->LogWarning("マテリアルが見つかりません(Missing): " + path);
		return nullptr;
	}

	const json j = json::parse(in, nullptr, false);
	if (j.is_discarded())
	{
		LOG->LogError(".mat の解析に失敗: " + path);
		return nullptr;
	}

	auto m = std::make_shared<Material>();
	m->Init();
	ApplyJson(*m, j);

	m_Cache[key] = m;
	return m;
}

bool MaterialLibrary::Save(const std::string& path)
{
	auto it = m_Cache.find(KeyOf(path));
	if (it == m_Cache.end()) return false;

	const Material& m = *it->second;
	if (!WriteJson(path, ToJson(m, m.shaderName.empty() ? "PBR" : m.shaderName))) return false;

	m_Dirty[KeyOf(path)] = false;
	LOG->LogInfo("マテリアルを保存: " + path);
	return true;
}

bool MaterialLibrary::IsDirty(const std::string& path) const
{
	const auto it = m_Dirty.find(KeyOf(path));
	return it != m_Dirty.end() && it->second;
}

void MaterialLibrary::MarkDirty(const std::string& path)
{
	if (path.empty()) return;
	m_Dirty[KeyOf(path)] = true;
}

std::string MaterialLibrary::CreateFromMaterial(
	const Material& src, const std::string& fallbackShader,
	const std::string& dir, const std::string& name)
{
	const fs::path file = UniquePath(dir, SanitizeFileName(name));
	const std::string path = file.generic_string();

	const std::string shader = !src.shaderName.empty() ? src.shaderName : fallbackShader;
	if (!WriteJson(path, ToJson(src, shader))) return {};

	ASSETDB->OnAssetAdded(path);
	LOG->LogInfo("マテリアルを書き出し: " + path);
	return path;
}

std::string MaterialLibrary::CreateDefault(const std::string& dir)
{
	Material m;   // Init しない(GPU リソースは要らない。既定値だけ使う)
	return CreateFromMaterial(m, "PBR", dir, "NewMaterial");
}

std::string MaterialLibrary::EnsureDefault()
{
	const std::string rel = "Assets/Materials/Default.mat";

	std::error_code ec;
	if (!fs::exists(Utf8ToPath(rel), ec))
	{
		fs::create_directories(Utf8ToPath("Assets/Materials"), ec);

		Material m;   // Init しない(GPU リソースは要らない。既定値だけ使う)
		if (!WriteJson(rel, ToJson(m, "PBR"))) return {};

		ASSETDB->OnAssetAdded(rel);
		LOG->LogInfo("既定のマテリアルを作成: " + rel);
	}
	return rel;
}

std::shared_ptr<Material> MaterialLibrary::Clone(const Material& src)
{
	auto m = std::make_shared<Material>();
	m->Init();
	ApplyJson(*m, ToJson(src, src.shaderName));
	m->shaderName = src.shaderName;   // 空(全体設定を継承)も保つ
	return m;
}

bool MaterialLibrary::SetTexture(const std::string& path, UINT slot, const std::string& texturePath)
{
	auto it = m_Cache.find(KeyOf(path));
	if (it == m_Cache.end()) return false;

	Material& m = *it->second;
	const bool srgb = (slot == TexSlot::Albedo || slot == TexSlot::Emissive);
	const std::wstring w = fs::path(ResolveAssetPath(texturePath)).wstring();

	APP->WaitForGPUIdle();   // 使用中のテクスチャを差し替えるので GPU を止める
	const bool ok = (slot == TexSlot::Ramp)
		? m.SetToonRampTexture(w)
		: m.Textures().LoadFromFile(slot, w, srgb);

	if (ok) m.Textures().SetSourcePath(slot, texturePath);
	return ok;
}