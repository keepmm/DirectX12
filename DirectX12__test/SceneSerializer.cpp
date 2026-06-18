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

using json = nlohmann::json;

namespace
{
	std::string ShapeTypeToString(ColliderComponent::ShapeType shapeType)
	{
		switch (shapeType)
		{
		case ColliderComponent::ShapeType::Box:
			return "Box";
		case ColliderComponent::ShapeType::Sphere:
			return "Sphere";
		case ColliderComponent::ShapeType::Capsule:
			return "Capsule";
		case ColliderComponent::ShapeType::Mesh:
			return "Mesh";
		default:
			return "Box";
		}
	}

	ColliderComponent::ShapeType ShapeTypeFromString(const std::string& str)
	{
		if (str == "Sphere")
			return ColliderComponent::ShapeType::Sphere;
		if (str == "Capsule")
			return ColliderComponent::ShapeType::Capsule;
		if (str == "Mesh")
			return ColliderComponent::ShapeType::Mesh;
		return ColliderComponent::ShapeType::Box;
	}
}

bool SceneSerializer::Save(Scene& scene, const std::string& filePath)
{
	// ディレクトリ作成
	std::ofstream out(filePath);
	if (!out) return false;
	out << SaveToString(scene);
	return true;
}

bool SceneSerializer::Load(Scene& scene, const std::string& filePath)
{
	std::ifstream in(filePath);
	if (!in)
	{
		return false;
	}

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
		if (world.HasComponent<PrefabComponent>(entity))
		{
			const auto& prefabComp = world.GetComponent<PrefabComponent>(entity);
			entry["prefab"] = prefabComp.name;
			if (!prefabComp.guid.empty())
			{
				entry["prefabGuid"] = prefabComp.guid;
			}
		}

		if (world.HasComponent<NameComponent>(entity))
		{
			entry["name"] = world.GetComponent<NameComponent>(entity).name;
		}

		if (world.HasComponent<TransformComponent>(entity))
		{
			const auto& t = world.GetComponent<TransformComponent>(entity);
			entry["transform"]["position"] = { t.position.x, t.position.y, t.position.z };
			entry["transform"]["rotation"] = { t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w };
			entry["transform"]["scale"] = { t.scale.x, t.scale.y, t.scale.z };
		}

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

		// mesh
		if (world.HasComponent<MeshComponent>(entity))
		{
			const auto& meshComp = world.GetComponent<MeshComponent>(entity);
			entry["mesh"]["filePath"] = meshComp.FilePath;
			entry["mesh"]["scale"] = meshComp.scale;
		}

		// material
		if (world.HasComponent<MaterialComponent>(entity))
		{
			const auto& matComp = world.GetComponent<MaterialComponent>(entity);
			entry["material"]["filePath"] = matComp.FilePath;
			entry["material"]["rampFilePath"] = matComp.RampFilePath;
			entry["material"]["usePixelShader"] = matComp.usePixelShader;
			entry["material"]["pixelShader"] = static_cast<int>(matComp.pixelshader);
		}

		// light
		if (world.HasComponent<LightComponent>(entity))
		{
			const auto& lightComp = world.GetComponent<LightComponent>(entity);
			entry["light"]["type"] = static_cast<int>(lightComp.type);
			entry["light"]["color"] = { lightComp.color.x, lightComp.color.y, lightComp.color.z, lightComp.color.w };
			entry["light"]["ambientColor"] = { lightComp.ambientColor.x, lightComp.ambientColor.y, lightComp.ambientColor.z, lightComp.ambientColor.w };
			entry["light"]["intensity"] = lightComp.intensity;
			entry["light"]["range"] = lightComp.range;
			entry["light"]["direction"] = { lightComp.direction.x, lightComp.direction.y, lightComp.direction.z };
			entry["light"]["spotAngle"] = lightComp.spotAngle;
			entry["light"]["isActive"] = lightComp.isActive;
			entry["light"]["castShadows"] = lightComp.castShadows;
		}

		// camera
		if(world.HasComponent<CameraComponent>(entity))
		{
			const auto& camComp = world.GetComponent<CameraComponent>(entity);
			entry["camera"]["projection"] = static_cast<int>(camComp.projection);
			entry["camera"]["cameraType"] = static_cast<int>(camComp.cameraType);
			entry["camera"]["orhoSize"] = camComp.orhoSize;
			entry["camera"]["fovY"] = camComp.fovY;
			entry["camera"]["nearZ"] = camComp.nearZ;
			entry["camera"]["farZ"] = camComp.farZ;
			entry["camera"]["isActive"] = camComp.isActive;
		}

		// free look
		if(world.HasComponent<FreeLookComponent>(entity))
		{
			const auto& freeLookComp = world.GetComponent<FreeLookComponent>(entity);
			entry["freelook"]["moveSpeed"] = freeLookComp.moveSpeed;
			entry["freelook"]["rotateSpeed"] = freeLookComp.rotateSpeed;
			entry["freelook"]["yaw"] = freeLookComp.yaw;
			entry["freelook"]["pitch"] = freeLookComp.pitch;
			entry["freelook"]["enabled"] = freeLookComp.Enabled;
		}

		// spin 
		if(world.HasComponent<SpinComponent>(entity))
		{
			const auto& spinComp = world.GetComponent<SpinComponent>(entity);
			entry["spin"]["speed"] = spinComp.speed;
			entry["spin"]["angle"] = spinComp.angle;
		}

		root["entities"].push_back(std::move(entry));
	}
	return root.dump(2);
}

bool SceneSerializer::LoadFromString(Scene& scene, const std::string& data)
{
	json root;
	root = json::parse(data,nullptr,false);

	if (!root.contains("entities"))
	{
		return false;
	}

	scene.ResetWorld();

	World& world = scene.GetWorld();
	PhysicsWorld* physicsWorld = nullptr;

	// シーン名のロード
	if (root.contains("sceneName"))
	{
		scene.SetSceneName(root["sceneName"].get<std::string>());
	}

	for (const auto& entry : root["entities"])
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
			{
				entity = PrefabLibrary::Get().Instantiate(prefab, scene, world);
			}
		}

		if (entity == INVALID_ENTITY)
		{
			const bool isPrefab = entry.contains("prefab") || entry.contains("prefabGuid");
			if (isPrefab)
			{
				continue;	// プレハブ指定なのに見つからない場合だけスキップ
			}
			entity = world.CreateEntity();	// 非プレハブは素のエンティティとして復元
		}

		if (entry.contains("name"))
		{
			const std::string name = entry["name"].get<std::string>();
			if (world.HasComponent<NameComponent>(entity))
			{
				world.GetComponent<NameComponent>(entity).name = name;
			}
			else
			{
				world.AddComponent<NameComponent>(entity, NameComponent{ name });
			}
		}

		if (entry.contains("transform"))
		{
			auto t = TransformComponent{};
			const auto& pos = entry["transform"]["position"];
			const auto& rot = entry["transform"]["rotation"];
			const auto& scale = entry["transform"]["scale"];

			t.position = float3(pos[0], pos[1], pos[2]);
			t.rotation = float4(rot[0], rot[1], rot[2], rot[3]);
			t.scale = float3(scale[0], scale[1], scale[2]);
			t.RebuildWorld();

			if (world.HasComponent<TransformComponent>(entity))
			{
				world.GetComponent<TransformComponent>(entity) = t;
			}
			else
			{
				world.AddComponent<TransformComponent>(entity, t);
			}
		}

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
			{
				world.GetComponent<RigidBodyComponent>(entity) = rb;
			}
			else
			{
				world.AddComponent<RigidBodyComponent>(entity, rb);
			}
		}

		if (hasCol)
		{
			ColliderComponent col{};
			const auto& colJson = entry["collider"];
			col.shapeType = ShapeTypeFromString(colJson.value("shape", "Box"));

			if (colJson.contains("size"))
			{
				const auto& size = colJson["size"];
				col.size = float3(size[0], size[1], size[2]);
			}

			col.radius = colJson.value("radius", 0.5f);
			col.friction = colJson.value("friction", 0.5f);
			col.restitution = colJson.value("restitution", 0.5f);
			col.density = colJson.value("density", 1.0f);

			if (world.HasComponent<ColliderComponent>(entity))
			{
				world.GetComponent<ColliderComponent>(entity) = col;
			}
			else
			{
				world.AddComponent<ColliderComponent>(entity, col);
			}
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
			mat.usePixelShader = mj.value("usePixelShader", false);
			mat.pixelshader = static_cast<E_PIXEL_SHADER>(mj.value("pixelShader", 0));

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

		// mesh
		if (entry.contains("mesh"))
		{
			const auto& meshJson = entry["mesh"];
			MeshComponent meshComp{};
			meshComp.FilePath = meshJson.value("filePath", "");
			meshComp.scale = meshJson.value("scale", 1.0f);

			if (!meshComp.FilePath.empty())
			{
				auto result = ModelLoader::LoadFromFile(APP->GetDevice(),meshComp.FilePath,meshComp.scale);
				meshComp.mesh = result.mesh;
			}

			if (world.HasComponent<MeshComponent>(entity))
			{
				world.GetComponent<MeshComponent>(entity) = meshComp;
			}
			else
			{
				world.AddComponent<MeshComponent>(entity, meshComp);
			}
		}

		// ---- Light ---- //
		if (entry.contains("light"))
		{
			const auto& lj = entry["light"];
			LightComponent l{};
			l.type = static_cast<LightComponent::LightType>(lj.value("type", 0));
			if (lj.contains("color"))
			{
				const auto& c = lj["color"];
				l.color = float4(c[0], c[1], c[2], c[3]);
			}
			if (lj.contains("ambient"))
			{
				const auto& a = lj["ambient"];
				l.ambientColor = float4(a[0], a[1], a[2], a[3]);
			}
			l.intensity = lj.value("intensity", 1.0f);
			l.range = lj.value("range", 10.0f);
			l.spotAngle = lj.value("spotAngle", 45.0f);
			l.isActive = lj.value("isActive", true);

			if (world.HasComponent<LightComponent>(entity))
				world.GetComponent<LightComponent>(entity) = l;
			else
				world.AddComponent<LightComponent>(entity, l);
		}

		// ---- camera ---- //
		if (entry.contains("camera"))
		{
			const auto& cj = entry["camera"];
			CameraComponent cam{};
			cam.projection = static_cast<CameraComponent::Projection>(cj.value("projection", 0));
			cam.cameraType = static_cast<CameraComponent::CameraType>(cj.value("cameraType", 0));
			cam.orhoSize = cj.value("orhoSize", 10.0f);
			cam.fovY = cj.value("fovY", 60.0f);
			cam.nearZ = cj.value("nearZ", 0.1f);
			cam.farZ = cj.value("farZ", 100.0f);
			cam.isActive = cj.value("isActive", true);
			if (world.HasComponent<CameraComponent>(entity))
				world.GetComponent<CameraComponent>(entity) = cam;
			else
				world.AddComponent<CameraComponent>(entity, cam);
		}


		// ---- free look ---- //
		if (entry.contains("freelook"))
		{
			const auto& fj = entry["freelook"];
			FreeLookComponent freeLook{};
			freeLook.moveSpeed = fj.value("moveSpeed", 5.0f);
			freeLook.rotateSpeed = fj.value("rotateSpeed", 1.0f);
			freeLook.yaw = fj.value("yaw", 0.0f);
			freeLook.pitch = fj.value("pitch", 0.0f);
			freeLook.Enabled = fj.value("enabled", false);
			if (world.HasComponent<FreeLookComponent>(entity))
				world.GetComponent<FreeLookComponent>(entity) = freeLook;
			else
				world.AddComponent<FreeLookComponent>(entity, freeLook);
		}

		// ---- spin ---- //
		if (entry.contains("spin"))
		{
			const auto& sj = entry["spin"];
			SpinComponent spin{};
			spin.speed = sj.value("speed", 0.0f);
			spin.angle = sj.value("angle", 0.0f);
			if (world.HasComponent<SpinComponent>(entity))
				world.GetComponent<SpinComponent>(entity) = spin;
			else
				world.AddComponent<SpinComponent>(entity, spin);
		}
	}

	return true;
}
