#include "SceneSerializer.hpp"
#include "Scene.hpp"
#include "Components.hpp"
#include "PrefabLibrary.hpp"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include "DirectX.hpp"
#include "Material.hpp"
#include "ModelLoader.hpp"
#include "ComponentRegistry.hpp"
#include "MonoBehavior.hpp"
#include "Logger.hpp"
#include "Util.hpp"
#include "AssetDatabase.hpp"
#include "MaterialLibrary.hpp"

using json = nlohmann::json;

namespace
{
    std::string ShapeTypeToString(ColliderComponent::ShapeType shapeType)
    {
        switch (shapeType)
        {
        case ColliderComponent::ShapeType::Box:     return "Box";
        case ColliderComponent::ShapeType::Sphere:  return "Sphere";
        case ColliderComponent::ShapeType::Capsule: return "Capsule";
        case ColliderComponent::ShapeType::Mesh:    return "Mesh";
        default: return "Box";
        }
    }

    ColliderComponent::ShapeType ShapeTypeFromString(const std::string& str)
    {
        if (str == "Sphere")  return ColliderComponent::ShapeType::Sphere;
        if (str == "Capsule") return ColliderComponent::ShapeType::Capsule;
        if (str == "Mesh")    return ColliderComponent::ShapeType::Mesh;
        return ColliderComponent::ShapeType::Box;
    }

    /// @brief JSON配列を安全にfloat3に変換する
	/// @param j jsonオブジェクト
	/// @param def デフォルト値
	/// @return 変換結果のfloat3
    float3 ToFloat3(_In_ const json& j, _In_ const float3& def = { 0.0f,0.0f,0.0f })
    {
        if (!j.is_array() || j.size() < 3) return def;
        if (!j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return def;
        return float3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    }

    /// @brief JSON配列を安全にfloat4に変換する
    /// @param j jsonオブジェクトj 
    /// @param def デフォルト値def 
    /// @return 変換結果のfloat4 
    float4 ToFloat4(_In_ const json& j, _In_ const float4& def = { 0.0f,0.0f,0.0f,1.0f })
    {
        if (!j.is_array() || j.size() < 4) return def;
        for (int i = 0; i < 4; ++i) if (!j[i].is_number()) return def;
        return float4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
	}

	/// @brief JSONオブジェクトから安全に文字列を取得する
	/// @param j jsonオブジェクト
	/// @param def デフォルト値
	/// @return 変換結果の文字列
    std::string SafeString(_In_ const json& parent, _In_ const std::string& key, _In_ const std::string& def = "")
    {
        if (!parent.contains(key) || !parent[key].is_string()) return def;
        return parent[key].get<std::string>();
	}

    /// @brief subMaterials の 1 要素を読む(LoadFromString と Undo で共有)
    SubMaterialRestore ReadSubMaterial(const json& sj)
    {
        SubMaterialRestore r;
        if (sj.is_null()) return r;

        r.materialAsset = ReadAssetRef(sj, "materialAsset");
        r.shaderName = sj.value("shaderName", std::string(""));
        r.roughness = sj.value("roughness", 0.5f);
        r.metallic = sj.value("metallic", 0.0f);
        if (sj.contains("emissiveColor"))
        {
            const auto& e = sj["emissiveColor"];
            r.emissiveColor = { e[0], e[1], e[2], 1.0f };
        }
        r.emissiveStrength = sj.value("emissiveStrength", 1.0f);
        r.sssStrength = sj.value("sssStrength", 0.0f);
        r.sssWrap = sj.value("sssWrap", 0.4f);
        r.sssTrans = sj.value("sssTrans", 0.0f);
        r.reflectStrength = sj.value("reflectStrength", 0.0f);
        r.reflectFade = sj.value("reflectFade", 8.0f);
        r.reflectBlur = sj.value("reflectBlur", 1.0f);
        r.sheen = sj.value("sheen", 0.0f);
        // sssColor は RGB の 3 要素で保存している(以前は ToFloat4 で読んでいて常に既定値になっていた)
        {
            const float3 c = ToFloat3(sj.value("sssColor", json::array()), float3(0.9f, 0.35f, 0.25f));
            r.sssColor = { c.x, c.y, c.z, 1.0f };
        }
        r.baseAlpha = sj.value("baseAlpha", 1.0f);

        // 以前のシーンには無い項目。キーがあるときだけ入れる
        if (sj.contains("baseColor"))    r.baseColor = ToFloat4(sj["baseColor"], float4(1, 1, 1, 1));
        if (sj.contains("rimColor"))     r.rimColor = ToFloat4(sj["rimColor"], float4(1, 1, 1, 1));
        if (sj.contains("outlineWidth")) r.outlineWidth = sj.value("outlineWidth", 1.0f);
        if (sj.contains("isFace"))       r.isFace = sj.value("isFace", false);
        return r;
    }

    /// @brief スクリプト 1 つ分の values を読む(LoadFromString と Undo で共有)
    std::unordered_map<std::string, FieldValue> ReadScriptValues(const json& values)
    {
        std::unordered_map<std::string, FieldValue> out;
        for (auto& [fieldName, jv] : values.items())
        {
            FieldValue fv;
            if (jv.is_boolean()) { fv.type = FieldType::Bool;   fv.b = jv.get<bool>(); }
            else if (jv.is_string()) { fv.type = FieldType::String; fv.s = jv.get<std::string>(); }
            // アセット参照 { guid, path }。'|' 区切りの複数はその配列。
            // 数値配列の分岐より前で拾わないと get<float> で例外になる
            else if (jv.is_object()
                || (jv.is_array() && !jv.empty() && jv[0].is_object()))
            {
                fv.type = FieldType::AssetPath;
                fv.s = AssetRefFromJson(jv);
            }
            else if (jv.is_number_integer()) { fv.type = FieldType::Int;    fv.i = jv.get<int>(); }
            else if (jv.is_number()) { fv.type = FieldType::Float;  fv.f[0] = jv.get<float>(); }
            else if (jv.is_array())
            {
                int n = (int)jv.size();
                fv.type = (n == 2) ? FieldType::Float2
                    : (n == 3) ? FieldType::Float3 : FieldType::Float4;
                for (int k = 0; k < n && k < 4; ++k) fv.f[k] = jv[k].get<float>();
            }
            out[fieldName] = fv;
        }
        return out;
    }

    /// @brief サブマテリアルの値を Material に当てる(ModelLoader の復元と同じ項目)
    void ApplySubMaterial(Material& m, const SubMaterialRestore& r)
    {
        m.shaderName = r.shaderName;
        m.roughness = r.roughness;
        m.metallic = r.metallic;
        m.sssStrength = r.sssStrength;
        m.sssWrap = r.sssWrap;
        m.sssTrans = r.sssTrans;
        m.sheen = r.sheen;
        m.sssColor = r.sssColor;
        m.baseAlpha = r.baseAlpha;
        m.reflectStrength = r.reflectStrength;
        m.reflectFade = r.reflectFade;
        m.reflectBlur = r.reflectBlur;
        m.emissiveColor = r.emissiveColor;
        m.emissiveStrength = r.emissiveStrength;
        if (r.baseColor)    m.baseColor = *r.baseColor;
        if (r.rimColor)     m.rimColor = *r.rimColor;
        if (r.outlineWidth) m.outlineWidth = *r.outlineWidth;
        if (r.isFace)       m.isFace = *r.isFace;
    }
}

bool SceneSerializer::Save(Scene& scene, const std::string& filePath)
{
    // Assets/Scenes を消した状態でも保存できるようにしておく
    std::error_code ec;
    const auto dir = std::filesystem::path(filePath).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);

    std::ofstream out(filePath);
    if (!out) return false;
    out << SaveToString(scene);
    return true;
}

bool SceneSerializer::Load(Scene& scene, const std::string& filePath)
{
    std::ifstream in(filePath);
    if (!in) return false;

    std::string text((std::istreambuf_iterator<char>(in)), {});
    return LoadFromString(scene, text);
}

std::string SceneSerializer::SaveToString(Scene& scene)
{
    json root;
    root["version"] = 2;   // 1 = パスのみ / 2 = アセット参照が {guid, path}
    root["sceneName"] = scene.GetSceneName();
    root["entities"] = json::array();
    WriteAssetRef(root, "skybox", scene.GetSkyboxPath());

    World& world = scene.GetWorld();
    for (Entity entity : world.GetEntities())
    {
        root["entities"].push_back(SaveEntity(world, entity));
    }

    return root.dump(2);
}

bool SceneSerializer::LoadFromString(Scene& scene, const std::string& data)
{
    json root;
    root = json::parse(data, nullptr, false);

    if (!root.contains("entities"))
        return false;

    scene.ResetWorld();

    World& world = scene.GetWorld();

    std::unordered_map<Entity, Entity> idMap; // 保存id -> 実Entity
    std::vector<std::pair<const json*, Entity>> loaded;

    if (root.contains("sceneName") && root["sceneName"].is_string())
        scene.SetSceneName(root["sceneName"].get<std::string>());
    if (root.contains("skybox"))
        scene.SetSkyboxPath(ReadAssetRef(root, "skybox"));

    for (const auto& entry : root["entities"])
    {
        try
        {
            Entity entity = INVALID_ENTITY;

            const std::string prefabGuid = entry.value("prefabGuid", "");
            if (!prefabGuid.empty())
            {
                entity = PrefabLibrary::Get().InstantiateByGuid(prefabGuid, scene, world);
            }
            else
            {
                const std::string prefab = entry.value("prefab", "");
                if (!prefab.empty())
                    entity = PrefabLibrary::Get().Instantiate(prefab, scene, world);
            }

            if (entity == INVALID_ENTITY)
            {
                const bool isPrefab = entry.contains("prefab") || entry.contains("prefabGuid");
                if (isPrefab)
                    continue; // プレハブ指定なのに見つからない場合はスキップ
                entity = world.CreateEntity(); // 非プレハブは素のエンティティとして復元
            }

            // entity が確定してから対応表に積む。作る前に積むと
// 「保存id → INVALID_ENTITY」になり、後段の親子の張り直しが全部潰れる
            if (entry.contains("id"))
                idMap[entry["id"].get<Entity>()] = entity;

            loaded.push_back({ &entry, entity });

            // ---- Name ---- //
            if (entry.contains("name") && entry["name"].is_string())
            {
                const std::string name = entry["name"].get<std::string>();
                if (world.HasComponent<NameComponent>(entity))
                    world.GetComponent<NameComponent>(entity).name = name;
                else
                    world.AddComponent<NameComponent>(entity, NameComponent{ name });
            }

            // ---- Transform ---- //
            if (entry.contains("transform"))
            {
                const auto& tj = entry["transform"];
                auto t = TransformComponent{};
                t.position = ToFloat3(tj.value("position", json::array()));
                t.rotation = ToFloat4(tj.value("rotation", json::array()), float4(0, 0, 0, 1));
                t.scale = ToFloat3(tj.value("scale", json::array()), float3(1, 1, 1));
                t.parent = (Entity)tj.value("parent", 0u);
                t.SyncEulerFromQuaternion();
                t.RebuildWorld();

                if (world.HasComponent<TransformComponent>(entity))
                    world.GetComponent<TransformComponent>(entity) = t;
                else
                    world.AddComponent<TransformComponent>(entity, t);
            }

            // ---- RigidBody / Collider ---- //
            const bool hasRb = entry.contains("rigidbody");
            const bool hasCol = entry.contains("collider");

            if (hasRb)
            {
                RigidBodyComponent rb{};
                const auto& rbJson = entry["rigidbody"];
                rb.mass = rbJson.value("mass", 1.0f);
                rb.isKinematic = rbJson.value("isKinematic", false);
                rb.isStatic = rbJson.value("isStatic", false);

                if (world.HasComponent<RigidBodyComponent>(entity))
                    world.GetComponent<RigidBodyComponent>(entity) = rb;
                else
                    world.AddComponent<RigidBodyComponent>(entity, rb);
            }

            if (hasCol)
            {
                ColliderComponent col{};
                const auto& colJson = entry["collider"];
                col.shapeType = ShapeTypeFromString(colJson.value("shape", "Box"));

                if (colJson.contains("size"))
                    col.size = ToFloat3(colJson["size"], float3(1, 1, 1));

                col.radius = colJson.value("radius", 0.5f);
                col.friction = colJson.value("friction", 0.5f);
                col.restitution = colJson.value("restitution", 0.5f);
                col.density = colJson.value("density", 1.0f);

                if (world.HasComponent<ColliderComponent>(entity))
                    world.GetComponent<ColliderComponent>(entity) = col;
                else
                    world.AddComponent<ColliderComponent>(entity, col);
            }

            // ---- Material ---- //
            if (entry.contains("material"))
            {
                const auto& mj = entry["material"];
                MaterialComponent mat{};
                mat.FilePath = ReadAssetRef(mj, "filePath");
                mat.RampFilePath = ReadAssetRef(mj, "rampPath");

                if (mj.contains("shaderName"))
                    mat.shaderName = mj.value("shaderName", std::string("Basic"));
                else {
                    static const char* kLegacy[] = { "Basic", "Toon" };   // 旧enum順
                    int idx = mj.value("pixelShader", 0);
                    mat.shaderName = (idx >= 0 && idx < 2) ? kLegacy[idx] : "Basic";
                }

                mat.material = std::make_shared<Material>();
                mat.material->Init();
                if (mj.contains("subMaterials"))
                {
                    for (const auto& sj : mj["subMaterials"])
                    {
                        mat.pendingSubs.push_back(ReadSubMaterial(sj));
                    }
                }
                if (!mat.FilePath.empty())
                    mat.material->SetTextureFromFile(std::filesystem::path(mat.FilePath).wstring());
                if (!mat.RampFilePath.empty())
                    mat.material->SetToonRampTexture(std::filesystem::path(mat.RampFilePath).wstring());

                if (world.HasComponent<MaterialComponent>(entity))
                    world.GetComponent<MaterialComponent>(entity) = mat;
                else
                    world.AddComponent<MaterialComponent>(entity, mat);
            }

            // ---- Mesh ---- //
            if (entry.contains("mesh"))
            {
                const auto& meshJson = entry["mesh"];
                MeshComponent meshComp{};
                meshComp.FilePath = ReadAssetRef(meshJson, "filePath");
                meshComp.scale = meshJson.value("scale", 1.0f);
                world.AddComponent<MeshComponent>(entity, meshComp);   // FilePathだけ先に確保

                if (IsPrimitivePath(meshComp.FilePath))
                {
                    // プリミティブはファイルを読まずに作り直す
					BuildPrimitiveEntity(world, entity, meshComp.FilePath);
                    ModelLoader::ApplyPendingSubMaterials(world.GetComponent<MaterialComponent>(entity));
                }
                else if (!meshComp.FilePath.empty())
                {
                    int  clip = 0; bool playing = false;
                    std::vector<std::string> extras;
                    if (entry.contains("animator"))
                    {
                        clip = entry["animator"].value("currentClip", 0);
                        playing = entry["animator"].value("playing", false);
                        if (entry["animator"].contains("extraClips"))
                            extras = entry["animator"]["extraClips"].get<std::vector<std::string>>();
                    }

                    ModelLoader::PopulateModelEntity(world, entity, meshComp.FilePath, &scene,
                        clip, playing, extras, false);
                }
            }

            // ---- それ以外の汎用コンポーネント（Reflect経由で自動） ---- //
            for (auto& c : ComponentRegistry::All())
            {
                if (c.load) c.load(world, entity, entry);
            }

            // ---- Script ---- //
            if (entry.contains("scripts"))
            {
                ScriptComponent sc{};
                for (const auto& one : entry["scripts"])
                {
                    const std::string name = one.value("name", "");
                    if (name.empty()) continue;
                    sc.scriptNames.push_back(name);

                    // フィールド値の復元
                    if (one.contains("values"))
                    {
                        sc.values[name] = ReadScriptValues(one["values"]);
                    }
                }
                if (!sc.scriptNames.empty())
                {
                    if (!world.HasComponent<ScriptComponent>(entity))
                        world.AddComponent<ScriptComponent>(entity, ScriptComponent{});

                    auto& dst = world.GetComponent<ScriptComponent>(entity);
                    dst.scriptNames = std::move(sc.scriptNames);
                    dst.values = std::move(sc.values);
                    dst.fieldDescs.clear();
                    dst.behaviors.clear();
                }
            }
        }
        catch (const std::exception& ex)
        {
            LOG->LogError(std::string("[Scene] エンティティ読込失敗: ") + ex.what());
            continue;   // 壊れたエンティティはスキップ、続行
        }
    }

    for (auto& [entryPtr, entity] : loaded)
    {
        const json& entry = *entryPtr;
        if (entry.contains("transform") && entry["transform"].contains("parent"))
        {
            Entity savedParent = entry["transform"]["parent"].get<Entity>();
            if (savedParent != INVALID_ENTITY && world.HasComponent<TransformComponent>(entity))
            {
                auto it = idMap.find(savedParent);
                world.GetComponent<TransformComponent>(entity).parent =
                    (it != idMap.end()) ? it->second : INVALID_ENTITY;
            }
        }
    }

    return true;
}

nlohmann::json SceneSerializer::SaveEntity(World& world, Entity entity)
{
    json entry;
    entry["id"] = entity;

    // ---- Prefab ---- //
    if (world.HasComponent<PrefabComponent>(entity))
    {
        const auto& prefabComp = world.GetComponent<PrefabComponent>(entity);
        entry["prefab"] = prefabComp.name;
        if (!prefabComp.guid.empty())
            entry["prefabGuid"] = prefabComp.guid;
    }

    // ---- Name ---- //
    if (world.HasComponent<NameComponent>(entity))
    {
        entry["name"] = world.GetComponent<NameComponent>(entity).name;
    }

    // ---- Transform ---- //
    if (world.HasComponent<TransformComponent>(entity))
    {
        const auto& t = world.GetComponent<TransformComponent>(entity);
        entry["transform"]["position"] = { t.position.x, t.position.y, t.position.z };
        entry["transform"]["rotation"] = { t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w };
        entry["transform"]["scale"] = { t.scale.x, t.scale.y, t.scale.z };
        entry["transform"]["parent"] = t.parent;
    }

    // ---- RigidBody / Collider） ---- //
    if (world.HasComponent<RigidBodyComponent>(entity))
    {
        const auto& rb = world.GetComponent<RigidBodyComponent>(entity);
        entry["rigidbody"]["mass"] = rb.mass;
        entry["rigidbody"]["isKinematic"] = rb.isKinematic;
        entry["rigidbody"]["isStatic"] = rb.isStatic;
    }

    if (world.HasComponent<ColliderComponent>(entity))
    {
        const auto& col = world.GetComponent<ColliderComponent>(entity);
        entry["collider"]["shape"] = ShapeTypeToString(col.shapeType);
        entry["collider"]["size"] = { col.size.x, col.size.y, col.size.z };
        entry["collider"]["radius"] = col.radius;
        entry["collider"]["friction"] = col.friction;
        entry["collider"]["restitution"] = col.restitution;
        entry["collider"]["density"] = col.density;
    }

    // ---- Mesh ---- //
    if (world.HasComponent<MeshComponent>(entity))
    {
        const auto& meshComp = world.GetComponent<MeshComponent>(entity);
        WriteAssetRef(entry["mesh"], "filePath", meshComp.FilePath);
        entry["mesh"]["scale"] = meshComp.scale;
    }

    if (world.HasComponent<AnimatorComponent>(entity))
    {
        const auto& an = world.GetComponent<AnimatorComponent>(entity);
        entry["animator"]["currentClip"] = an.currentClip;
        entry["animator"]["playing"] = an.playing;
        entry["animator"]["extraClips"] = an.extraClipNames;
    }

    // ---- Material ---- //
    if (world.HasComponent<MaterialComponent>(entity))
    {
        const auto& matComp = world.GetComponent<MaterialComponent>(entity);
        entry["material"]["shaderName"] = matComp.shaderName;
        WriteAssetRef(entry["material"], "filePath", matComp.FilePath);
        WriteAssetRef(entry["material"], "rampPath", matComp.RampFilePath);
        auto& subs = entry["material"]["subMaterials"] = nlohmann::json::array();
        for (size_t i = 0; i < matComp.materials.size(); ++i)
        {
            const auto& sm = matComp.materials[i];
            const std::string asset =
                (i < matComp.materialAssets.size()) ? matComp.materialAssets[i] : std::string{};

            nlohmann::json sj;
            if (!asset.empty())
            {
                // 共有マテリアル: 値は .mat 側にあるので参照だけ書く
                WriteAssetRef(sj, "materialAsset", asset);
            }
            else if (sm)
            {
                sj["shaderName"] = sm->shaderName;
                sj["roughness"] = sm->roughness;
                sj["metallic"] = sm->metallic;
                sj["sssStrength"] = sm->sssStrength;
                sj["sssWrap"] = sm->sssWrap;
                sj["sssTrans"] = sm->sssTrans;
                sj["reflectStrength"] = sm->reflectStrength;
                sj["reflectFade"] = sm->reflectFade;
                sj["reflectBlur"] = sm->reflectBlur;
                sj["sheen"] = sm->sheen;
                sj["sssColor"] = { sm->sssColor.x, sm->sssColor.y, sm->sssColor.z };
                sj["emissiveColor"] = { sm->emissiveColor.x, sm->emissiveColor.y, sm->emissiveColor.z };
                sj["emissiveStrength"] = sm->emissiveStrength;
                sj["baseAlpha"] = sm->baseAlpha;
                sj["baseColor"] = { sm->baseColor.x, sm->baseColor.y, sm->baseColor.z, sm->baseColor.w };
                sj["rimColor"] = { sm->rimColor.x, sm->rimColor.y, sm->rimColor.z, sm->rimColor.w };
                sj["outlineWidth"] = sm->outlineWidth;
                sj["isFace"] = sm->isFace;
            }
            subs.push_back(sj);
        }
    }

    // ---- それ以外の汎用コンポーネント ---- //
    for (auto& c : ComponentRegistry::All())
    {
        if (c.save) c.save(world, entity, entry);
    }

    // ---- Script ---- //
    if (world.HasComponent<ScriptComponent>(entity))
    {
        const auto& sc = world.GetComponent<ScriptComponent>(entity);
        json scriptsJson = json::array();
        for (const auto& name : sc.scriptNames)
        {
            json one;
            one["name"] = name;

            // フィールド値(values)も保存
            json vals;
            auto it = sc.values.find(name);
            if (it != sc.values.end())
            {
                for (const auto& [fieldName, v] : it->second)
                {
                    // FieldValue を型に応じてjson化
                    switch (v.type)
                    {
                    case FieldType::Int:    vals[fieldName] = v.i; break;
                    case FieldType::Float:  vals[fieldName] = v.f[0]; break;
                    case FieldType::Float2: vals[fieldName] = { v.f[0], v.f[1] }; break;
                    case FieldType::Float3: vals[fieldName] = { v.f[0], v.f[1], v.f[2] }; break;
                    case FieldType::Color:
                    case FieldType::Float4: vals[fieldName] = { v.f[0], v.f[1], v.f[2], v.f[3] }; break;
                    case FieldType::Bool:   vals[fieldName] = v.b; break;
                    case FieldType::String: vals[fieldName] = v.s; break;
                    case FieldType::Entity: vals[fieldName] = v.i; break;  // EntityRefのid
                    case FieldType::Texture:
                    case FieldType::Font:
                    case FieldType::Audio:
                    case FieldType::AssetPath:
                        vals[fieldName] = AssetRefToJson(v.s);
                        break;
                    default: break;
                    }
                }
            }
            one["values"] = vals;
            scriptsJson.push_back(std::move(one));
        }
        entry["scripts"] = std::move(scriptsJson);
    }

    return entry;
}

namespace
{
    /// @brief マテリアルの json を、既存の MaterialComponent に当てる(作り直さない)
    void ApplyMaterialJson(World& world, Entity e, const json& mj)
    {
        auto& mc = world.GetComponent<MaterialComponent>(e);
        mc.shaderName = mj.value("shaderName", mc.shaderName);

        const std::string tex = ReadAssetRef(mj, "filePath");
        if (tex != mc.FilePath)
        {
            mc.FilePath = tex;
            if (mc.material && !tex.empty())
                mc.material->SetTextureFromFile(std::filesystem::path(tex).wstring());
        }
        const std::string ramp = ReadAssetRef(mj, "rampPath");
        if (ramp != mc.RampFilePath)
        {
            mc.RampFilePath = ramp;
            if (mc.material && !ramp.empty())
                mc.material->SetToonRampTexture(std::filesystem::path(ramp).wstring());
        }

        if (!mj.contains("subMaterials")) return;
        const json& subs = mj["subMaterials"];
        mc.materialAssets.resize(mc.materials.size());

        for (size_t i = 0; i < subs.size() && i < mc.materials.size(); ++i)
        {
            const SubMaterialRestore r = ReadSubMaterial(subs[i]);

            // .mat の割り当てが変わった(割り当て / 解除の Undo)
            if (r.materialAsset != mc.materialAssets[i])
            {
                auto m = r.materialAsset.empty()
                    ? (mc.materials[i] ? MaterialLibrary::Get().Clone(*mc.materials[i]) : nullptr)
                    : MaterialLibrary::Get().Load(r.materialAsset);
                if (m)
                {
                    APP->WaitForGPUIdle();
                    mc.materials[i] = m;
                    if (i == 0) mc.material = m;
                    mc.materialAssets[i] = r.materialAsset;
                }
            }

            // 共有マテリアルの値は .mat 側にあるので、Entity 側からは当てない
            if (!r.materialAsset.empty() || !mc.materials[i]) continue;
            ApplySubMaterial(*mc.materials[i], r);
        }
    }
}

void SceneSerializer::ApplyEntityDiff(Scene& scene, Entity e, const json& from, const json& to)
{
    World& world = scene.GetWorld();
    if (!world.IsEntityAlive(e)) return;

    // from と to でそのキーが違うか(片方にしか無い場合も含む)
    auto changed = [&](const std::string& key)
        {
            const bool a = from.contains(key), b = to.contains(key);
            if (a != b) return true;
            return a && from[key] != to[key];
        };

    // ---- Name ---- //
    if (changed("name"))
    {
        if (to.contains("name") && to["name"].is_string())
        {
            const std::string name = to["name"].get<std::string>();
            if (world.HasComponent<NameComponent>(e)) world.GetComponent<NameComponent>(e).name = name;
            else world.AddComponent<NameComponent>(e, NameComponent{ name });
        }
        else if (world.HasComponent<NameComponent>(e))
        {
            world.DeleteComponent<NameComponent>(e);
        }
    }

    // ---- Transform ---- //
    if (changed("transform"))
    {
        if (to.contains("transform"))
        {
            const json& tj = to["transform"];
            // 保存しない実行時の値を消さないよう、今の値を土台にする
            TransformComponent t = world.HasComponent<TransformComponent>(e)
                ? world.GetComponent<TransformComponent>(e) : TransformComponent{};
            t.position = ToFloat3(tj.value("position", json::array()));
            t.rotation = ToFloat4(tj.value("rotation", json::array()), float4(0, 0, 0, 1));
            t.scale = ToFloat3(tj.value("scale", json::array()), float3(1, 1, 1));
            t.parent = (Entity)tj.value("parent", 0u);
            t.SyncEulerFromQuaternion();
            t.RebuildWorld();

            if (world.HasComponent<TransformComponent>(e)) world.GetComponent<TransformComponent>(e) = t;
            else world.AddComponent<TransformComponent>(e, t);
        }
        else if (world.HasComponent<TransformComponent>(e))
        {
            world.DeleteComponent<TransformComponent>(e);
        }
    }

    // ---- Material ---- //
    if (changed("material") && to.contains("material") && world.HasComponent<MaterialComponent>(e))
    {
        ApplyMaterialJson(world, e, to["material"]);
    }

    // ---- Mesh(パスが変わったときだけ読み直す) ---- //
    if (changed("mesh") && to.contains("mesh") && world.HasComponent<MeshComponent>(e))
    {
        const json& mj = to["mesh"];
        auto& mc = world.GetComponent<MeshComponent>(e);
        mc.scale = mj.value("scale", mc.scale);

        const std::string path = ReadAssetRef(mj, "filePath");
        if (path != mc.FilePath)
        {
            if (IsPrimitivePath(path)) BuildPrimitiveEntity(world, e, path);
            else if (!path.empty())
				ModelLoader::PopulateModelEntity(world, e, path, &scene, 0, false, {}, false);
            ModelLoader::ApplyPendingSubMaterials(world.GetComponent<MaterialComponent>(e));
        }
    }

    // ---- Animator ---- //
    if (changed("animator") && to.contains("animator") && world.HasComponent<AnimatorComponent>(e))
    {
        auto& an = world.GetComponent<AnimatorComponent>(e);
        an.currentClip = to["animator"].value("currentClip", an.currentClip);
        an.playing = to["animator"].value("playing", an.playing);
    }

    // ---- RigidBody / Collider ---- //
    if (changed("rigidbody") && to.contains("rigidbody") && world.HasComponent<RigidBodyComponent>(e))
    {
        auto& rb = world.GetComponent<RigidBodyComponent>(e);
        const json& j = to["rigidbody"];
        rb.mass = j.value("mass", rb.mass);
        rb.isKinematic = j.value("isKinematic", rb.isKinematic);
        rb.isStatic = j.value("isStatic", rb.isStatic);
    }
    if (changed("collider") && to.contains("collider") && world.HasComponent<ColliderComponent>(e))
    {
        auto& col = world.GetComponent<ColliderComponent>(e);
        const json& j = to["collider"];
        col.shapeType = ShapeTypeFromString(j.value("shape", std::string("Box")));
        col.size = ToFloat3(j.value("size", json::array()), col.size);
        col.radius = j.value("radius", col.radius);
        col.friction = j.value("friction", col.friction);
        col.restitution = j.value("restitution", col.restitution);
        col.density = j.value("density", col.density);
    }

    // ---- 登録済みの汎用コンポーネント(追加 / 削除 / 値の変更) ---- //
    for (const auto& c : ComponentRegistry::All())
    {
        if (!changed(c.name)) continue;
        if (to.contains(c.name)) { if (c.load) c.load(world, e, to); }
        else if (c.remove)       { c.remove(world, e); }
    }

    // ---- スクリプトの値 ---- //
    if (changed("scripts") && to.contains("scripts") && world.HasComponent<ScriptComponent>(e))
    {
        auto& sc = world.GetComponent<ScriptComponent>(e);
        for (const auto& one : to["scripts"])
        {
            const std::string name = one.value("name", "");
            if (!name.empty() && one.contains("values"))
                sc.values[name] = ReadScriptValues(one["values"]);
        }
    }
}
