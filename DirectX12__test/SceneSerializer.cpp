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
}

bool SceneSerializer::Save(Scene& scene, const std::string& filePath)
{
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
    root["sceneName"] = scene.GetSceneName();
    root["entities"] = json::array();

    World& world = scene.GetWorld();
    for (Entity entity : world.GetEntities())
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
            entry["mesh"]["filePath"] = meshComp.FilePath;
            entry["mesh"]["scale"] = meshComp.scale;
        }

        // ---- Material ---- //
        if (world.HasComponent<MaterialComponent>(entity))
        {
            const auto& matComp = world.GetComponent<MaterialComponent>(entity);
            entry["material"]["filePath"] = matComp.FilePath;
            entry["material"]["rampFilePath"] = matComp.RampFilePath;
            entry["material"]["shaderName"] = matComp.shaderName;
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
                        default: break;
                        }
                    }
                }
                one["values"] = vals;
                scriptsJson.push_back(std::move(one));
            }
            entry["scripts"] = std::move(scriptsJson);
        }

        root["entities"].push_back(std::move(entry));
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
    PhysicsWorld* physicsWorld = nullptr;

    std::unordered_map<Entity, Entity> idMap; // 保存id -> 実Entity
    std::vector<std::pair<const json*, Entity>> loaded;

    if (root.contains("sceneName") && root["sceneName"].is_string())
        scene.SetSceneName(root["sceneName"].get<std::string>());

    for (const auto& entry : root["entities"])
    {
        try
        {
            Entity entity = INVALID_ENTITY;

            if (entry.contains("id"))
                idMap[entry["id"].get<Entity>()] = entity;

            loaded.push_back({ &entry, entity });

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

            if (hasRb && hasCol)
            {
                if (physicsWorld == nullptr)
                {
                    physicsWorld = &scene.EnsurePhysicsWorld();
                    physicsWorld->Init();
                }

                auto& rb = world.GetComponent<RigidBodyComponent>(entity);
                auto& col = world.GetComponent<ColliderComponent>(entity);

                physicsWorld->AddRigidbody(entity, rb, col);

                if (world.HasComponent<TransformComponent>(entity))
                {
                    const auto& t = world.GetComponent<TransformComponent>(entity);
                    physicsWorld->SetActorPose(entity, t.position, t.rotation);
                }
            }

            // ---- Material ---- //
            if (entry.contains("material"))
            {
                const auto& mj = entry["material"];
                MaterialComponent mat{};
                mat.FilePath = mj.value("filePath", "");
                mat.RampFilePath = mj.value("rampPath", "");

                if (mj.contains("shaderName"))
                    mat.shaderName = mj.value("shaderName", std::string("Basic"));
                else {
                    static const char* kLegacy[] = { "Basic", "Toon" };   // 旧enum順
                    int idx = mj.value("pixelShader", 0);
                    mat.shaderName = (idx >= 0 && idx < 2) ? kLegacy[idx] : "Basic";
                }

                mat.material = std::make_shared<Material>();
                mat.material->Init();
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
                meshComp.FilePath = meshJson.value("filePath", "");
                meshComp.scale = meshJson.value("scale", 1.0f);

                if (!meshComp.FilePath.empty())
                {
                    auto result = ModelLoader::LoadFromFile(APP->GetDevice(), meshComp.FilePath, meshComp.scale);
                    meshComp.mesh = result.mesh;
                }

                if (world.HasComponent<MeshComponent>(entity))
                    world.GetComponent<MeshComponent>(entity) = meshComp;
                else
                    world.AddComponent<MeshComponent>(entity, meshComp);
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
                        auto& vmap = sc.values[name];
                        for (auto& [fieldName, jv] : one["values"].items())
                        {
                            FieldValue fv;
                            if (jv.is_boolean()) { fv.type = FieldType::Bool;   fv.b = jv.get<bool>(); }
                            else if (jv.is_string()) { fv.type = FieldType::String; fv.s = jv.get<std::string>(); }
                            else if (jv.is_number_integer()) { fv.type = FieldType::Int;    fv.i = jv.get<int>(); }
                            else if (jv.is_number()) { fv.type = FieldType::Float;  fv.f[0] = jv.get<float>(); }
                            else if (jv.is_array())
                            {
                                int n = (int)jv.size();
                                fv.type = (n == 2) ? FieldType::Float2
                                    : (n == 3) ? FieldType::Float3 : FieldType::Float4;
                                for (int k = 0; k < n && k < 4; ++k) fv.f[k] = jv[k].get<float>();
                            }
                            vmap[fieldName] = fv;
                        }
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