#pragma once
#include <string>
#include <Windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")
#include "Material.hpp"
#include "ModelData.hpp"
#include "Debug.hpp"
#include "Mesh.hpp"
#include "World.hpp"
#include "Components.hpp"

// ---- UTF-8 <-> wide 変換 ---- //
inline std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}

inline std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

// ---- アセットのパス ---- //
// ダイアログが返すのは絶対パス。そのまま保存するとシーンが他のPCで開けなくなるので、
// 作業ディレクトリの下にあるものは相対パスへ畳んでから持つ。
inline std::string MakeAssetRelative(const std::string& path)
{
    if (path.empty()) return path;

    std::error_code ec;
    const std::filesystem::path p(path);
    if (!p.is_absolute()) return path;

    const auto rel = std::filesystem::relative(p, std::filesystem::current_path(), ec);
    if (ec || rel.empty()) return path;

    // 作業ディレクトリの外(".." で始まる)は畳まずそのまま返す
    auto s = rel.generic_string();
    if (s.rfind("..", 0) == 0) return path;
    return s;
}

// 保存済みの絶対パスを開き直すための救済。
// そのまま存在すればそれを使い、無ければ "Assets/" 以降を切り出して相対で探す。
// 別のPCで作られたシーンでも、Assets の中にあるものなら拾える
inline std::string ResolveAssetPath(const std::string& path)
{
    if (path.empty()) return path;

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return path;

    // 区切りを揃えてから "Assets" の位置を探す
    std::string norm = path;
    for (auto& c : norm) if (c == '\\') c = '/';

    const size_t at = norm.rfind("Assets/");
    if (at == std::string::npos) return path;

    const std::string tail = norm.substr(at);
    if (std::filesystem::exists(tail, ec)) return tail;
    return path;
}

// ---- ファイル選択ダイアログ ---- //
// filter は "Image\0*.png;*.jpg\0All\0*.*\0" 形式（ダブルNUL終端）
inline bool OpenFileDialog(std::wstring& out, const wchar_t* filter)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) { out = file; return true; }
    return false;
}

inline std::vector<std::shared_ptr<Material>> BuildMaterials(
    const ModelLoadResult& model, const std::string& shaderName)
{
    std::vector<std::shared_ptr<Material>> out;
    // テクスチャの共有元Material(リソースの実体を持つ)をパスごとに記録
    // 同じ画像を最初に載せたマテリアルとスロットを覚えておき、2 回目以降は GPU リソースを共有する。
    // 以前はベースカラーだけ共有していて、法線 / メタル / ラフ / 発光 / AO は
    // サブマテリアルの数だけ VRAM に載っていた
    struct TexOwner { Material* material; UINT slot; };
    std::unordered_map<std::wstring, TexOwner> texOwner;

    /// @brief 共有のキー。パスがあればパス、埋め込みテクスチャはデコード済み画像の実体
    ///        (ModelLoader は同じ画像に同じ shared_ptr を返す)。sRGB かどうかも含める
    auto keyOf = [](const std::wstring& path, const std::shared_ptr<DecodedImage>& img, bool srgb) -> std::wstring
        {
            std::wstring base = !path.empty() ? path
                : (img ? L"@" + std::to_wstring(static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(img.get()))) : L"");
            if (base.empty()) return base;
            return base + (srgb ? L"|srgb" : L"|linear");
        };

    /// @return 共有できたら true(呼び出し側は作らなくてよい)
    auto tryShare = [&texOwner](Material& m, UINT slot, const std::wstring& key) -> bool
        {
            if (key.empty()) return false;
            auto it = texOwner.find(key);
            if (it == texOwner.end()) return false;
            m.Textures().ShareSlot(slot, it->second.material->Textures(), it->second.slot);
            return true;
        };
    auto remember = [&texOwner](Material& m, UINT slot, const std::wstring& key)
        {
            if (!key.empty()) texOwner.emplace(key, TexOwner{ &m, slot });
        };

    // 元ファイルがあるテクスチャだけ記録する(glTF の埋め込み等は空のまま)。
    // .mat に書き出すときの参照に使う
    auto sourceOf = [](const std::wstring& w) -> std::string
        {
            if (w.empty()) return {};
            std::error_code ec;
            if (!std::filesystem::exists(w, ec)) return {};
            return MakeAssetRelative(std::filesystem::path(w).generic_string());
        };

    for (const auto& set : model.materials)
    {
        auto m = std::make_shared<Material>();
        m->Init();
		m->baseAlpha = set.diffuseColor.w;
		m->baseColor = set.diffuseColor;
        m->baseColor.w = 1.0f;

        // 肌系マテリアルは既定で SSS を有効化（PMX の日本語名 / FBX の英語名）
        {
            // set.name は UTF-8。/utf-8 オプションが無く素の "肌" は CP932 になるため、UTF-8 バイトを直接書く
            static const char* kSkinKeys[] = {
                "\xE8\x82\x8C",   // hada (肌)
                "\xE9\xA1\x94",   // kao (顔)
                "\xE4\xBD\x93",   // karada (体)
                "skin", "Skin", "face", "Face", "body", "Body"
            };
            for (const char* key : kSkinKeys)
            {
                if (set.name.find(key) != std::string::npos)
                {
                    m->sssStrength = 0.6f;
                    m->sssWrap     = 0.4f;
                    m->sssTrans    = 0.15f;
                    m->sssColor    = { 0.95f, 0.55f, 0.45f, 1.0f }; // 赤すぎない血色
                    m->roughness   = 0.7f;                          // 肌はテカらせない
                    break;
                }
            }
        }

        // ---- ベースカラー(sRGB) ----
        {
            const std::wstring key = keyOf(set.diffuse, set.diffuseImage, true);
            if (!tryShare(*m, TexSlot::Albedo, key) && set.diffuseImage && set.diffuseImage->ok &&
                m->CreateTextureFromRGBA(set.diffuseImage->width, set.diffuseImage->height, set.diffuseImage->pixels.data()))
                remember(*m, TexSlot::Albedo, key);
        }

        // ---- 法線(リニア) ----
        if (set.normalImage && set.normalImage->ok)
        {
            const std::wstring key = keyOf(set.normal, set.normalImage, false);
            if (!tryShare(*m, TexSlot::Normal, key) &&
                m->CreateNormalFromRGBA(set.normalImage->width, set.normalImage->height, set.normalImage->pixels.data()))
                remember(*m, TexSlot::Normal, key);
        }

        // glTF の metallicRoughness は 1 枚から B=metal / G=rough を分解しているので、
        // 同じ ormPath でもチャンネルごとに別のキーにする
        const std::wstring metalPath = !set.metal.empty() ? set.metal
            : (!set.ormPath.empty() ? set.ormPath + L"#metal" : L"");
        const std::wstring roughPath = !set.rough.empty() ? set.rough
            : (!set.ormPath.empty() ? set.ormPath + L"#rough" : L"");

        // ---- メタル(リニア) ---- 共有でも作成でも、マップがあるときは係数を 1 にする
        if (set.metalImage && set.metalImage->ok)
        {
            const std::wstring key = keyOf(metalPath, set.metalImage, false);
            if (!tryShare(*m, TexSlot::Metal, key) &&
                m->CreateMetalFromRGBA(set.metalImage->width, set.metalImage->height, set.metalImage->pixels.data()))
                remember(*m, TexSlot::Metal, key);
            m->metallic = 1.0f;    // マップがあるときは係数。既定は等倍
        }

        // ---- ラフ(リニア) ----
        if (set.roughImage && set.roughImage->ok)
        {
            const std::wstring key = keyOf(roughPath, set.roughImage, false);
            if (!tryShare(*m, TexSlot::Rough, key) &&
                m->CreateRoughFromRGBA(set.roughImage->width, set.roughImage->height, set.roughImage->pixels.data()))
                remember(*m, TexSlot::Rough, key);
            m->roughness = 1.0f;
        }

        // ---- 発光(sRGB) ----
        bool hasEmissiveTex = false;
        if (set.emissiveImage && set.emissiveImage->ok)
        {
            const std::wstring key = keyOf(set.emissive, set.emissiveImage, true);
            if (tryShare(*m, TexSlot::Emissive, key))
            {
                hasEmissiveTex = true;
            }
            else if (m->CreateEmissiveFromRGBA(set.emissiveImage->width, set.emissiveImage->height, set.emissiveImage->pixels.data()))
            {
                remember(*m, TexSlot::Emissive, key);
                hasEmissiveTex = true;
            }
        }
        if (hasEmissiveTex)
        {
            // テクスチャが貼れたときだけ、factor 未設定を白（テクスチャそのまま）とみなす
            m->emissiveColor = (set.emissiveColor.x <= 0.0f && set.emissiveColor.y <= 0.0f && set.emissiveColor.z <= 0.0f)
                ? COLOR{ 1.0f, 1.0f, 1.0f, 1.0f } : set.emissiveColor;
        }
        else
        {
            m->emissiveColor = set.emissiveColor;   // 既定は黒＝発光なし
        }

        // ---- AO(リニア) ----
        if (set.occlusionImage && set.occlusionImage->ok)
        {
            const std::wstring key = keyOf(set.occlusion, set.occlusionImage, false);
            if (!tryShare(*m, TexSlot::Occlusion, key) &&
                m->CreateOcclusionFromRGBA(set.occlusionImage->width, set.occlusionImage->height, set.occlusionImage->pixels.data()))
                remember(*m, TexSlot::Occlusion, key);
        }

        m->Textures().SetSourcePath(TexSlot::Albedo,    sourceOf(set.diffuse));
        m->Textures().SetSourcePath(TexSlot::Normal,    sourceOf(set.normal));
        m->Textures().SetSourcePath(TexSlot::Metal,     sourceOf(set.metal));
        m->Textures().SetSourcePath(TexSlot::Rough,     sourceOf(set.rough));
        m->Textures().SetSourcePath(TexSlot::Emissive,  sourceOf(set.emissive));
        m->Textures().SetSourcePath(TexSlot::Occlusion, sourceOf(set.occlusion));

        out.push_back(m);
        LOG->LogInfo("SubMat: " + set.name
            + " tex=" + WideToUtf8(set.diffuse)
            + " imgOk=" + std::to_string(set.diffuseImage && set.diffuseImage->ok));
    }
    return out;
}

inline constexpr const char* kPrimitiveSphere = "@Sphere";
inline constexpr const char* kPrimitiveCube = "@Cube";
inline constexpr const char* kPrimitiveCylinder = "@Cylinder";
inline constexpr const char* kPrimitiveCone = "@Cone";
inline constexpr const char* kPrimitiveCapsule = "@Capsule";
inline constexpr const char* kPrimitivePlane = "@Plane";

inline bool IsPrimitivePath(const std::string& path)
{
    return !path.empty() && path[0] == '@';

}

/// @brief プリミティブのメッシュとマテリアルをエンティティに載せる
inline void BuildPrimitiveEntity(World& world, Entity e, const std::string& tag)
{
    auto mesh = std::make_shared<Mesh>();
    if (tag == kPrimitiveCube)          mesh->CreateCube(APP->GetDevice());
    else if (tag == kPrimitiveCylinder) mesh->CreateCylinder();
    else if (tag == kPrimitiveCone)     mesh->CreateCone();
    else if (tag == kPrimitiveCapsule)  mesh->CreateCapsule();
    else if (tag == kPrimitivePlane)    mesh->CreatePlane();
    else                                mesh->CreateSphere();

    MeshComponent mc{};
    mc.mesh = mesh;
    mc.FilePath = tag;
    if (world.HasComponent<MeshComponent>(e)) world.GetComponent<MeshComponent>(e) = mc;
    else                                      world.AddComponent<MeshComponent>(e, mc);

    // マテリアル 泣ければ作
    MaterialComponent& mat = world.HasComponent<MaterialComponent>(e)
        ? world.GetComponent<MaterialComponent>(e)
		: world.AddComponent<MaterialComponent>(e, MaterialComponent{});

    if (!mat.material)
    {
        mat.shaderName = "Basic";
		mat.material = std::make_shared<Material>();
        mat.material->Init();
		mat.material->baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (mat.materials.empty())
    {
        mat.materials.push_back(mat.material);
		mat.materialnames.push_back("Primitive");
    }
    mat.materialAssets.resize(mat.materials.size());

    // 形に合った当たり判定を付ける。
    // 既にある場合は、ユーザーが調整しているかもしれないので触らない
    if (!world.HasComponent<ColliderComponent>(e))
    {
        ColliderComponent col{};
        if (tag == kPrimitiveSphere)
        {
            col.shapeType = ColliderComponent::ShapeType::Sphere;
            col.radius = 0.5f;
        }
        else if (tag == kPrimitiveCapsule)
        {
            // 見た目は「円柱部分1 + 両端の半球」。Collider の size.y は円柱部分の長さ
            col.shapeType = ColliderComponent::ShapeType::Capsule;
            col.radius = 0.5f;
            col.size = { 1.0f, 1.0f, 1.0f };
        }
        else if (tag == kPrimitivePlane)
        {
            // 平面は厚みが無いと当たらないので、薄い箱にする
            col.shapeType = ColliderComponent::ShapeType::Box;
            col.size = { 1.0f, 0.02f, 1.0f };
        }
        else
        {
            // 立方体 / 円柱 / 円錐は箱で囲む。
            // PhysX に円柱と円錐の形は無く、凸メッシュを焼く必要があるため
            col.shapeType = ColliderComponent::ShapeType::Box;
            col.size = { 1.0f, 1.0f, 1.0f };
        }
        world.AddComponent<ColliderComponent>(e, col);
    }
}

inline std::string ShiftJisUtf8(const std::string& sjis)
{
    int wlen = MultiByteToWideChar(932, 0, sjis.c_str(), (int)sjis.size(), nullptr, 0);
	std::wstring w(wlen, L'\0');
	MultiByteToWideChar(932, 0, sjis.c_str(), (int)sjis.size(), w.data(), wlen);
	return WideToUtf8(w);
}

inline std::filesystem::path ResolveAssetPath(const std::filesystem::path& path)
{
	// 絶対パスまたは存在するパスならそのまま返す
    if (path.is_absolute() || std::filesystem::exists(path))
    {
        return path;
    }

    wchar_t exe[MAX_PATH]{};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / path;
}

/// @brief エンジン同梱リソースのパスを解決する
/// @note ユーザープロジェクトの Assets とは別管理。exe 横の EngineAssets を基準にする。
///       CWD はプロジェクトルートへ移動しているので、必ず exe 基準で組み立てること
inline std::filesystem::path EngineAssetPath(const std::filesystem::path& relative)
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / L"EngineAssets" / relative;
}