#include "SceneSerializer.hpp"
#include "Scene.hpp"
#include "Components.hpp"
#include "PrefabLibrary.hpp"
#include "json.hpp"
#include <filesystem>
#include <fstream>

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
	json root;
	root["sceneName"] = scene.GetSceneName();
	root["entities"] = json::array();

	World& world = scene.GetWorld();
	for (Entity entity : world.GetEntities())
	{
		if (!world.HasComponent<PrefabComponent>(entity))
		{
			continue;
		}

		json entry;
		const auto& prefabComp = world.GetComponent<PrefabComponent>(entity);
		entry["prefab"] = prefabComp.name;
		if (!prefabComp.guid.empty())
		{
			entry["prefabGuid"] = prefabComp.guid;
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

		root["entities"].push_back(std::move(entry));
	}

	std::filesystem::path path(filePath);
	if (path.has_parent_path())
	{
		std::filesystem::create_directories(path.parent_path());
	}

	std::ofstream out(filePath);
	if (!out)
	{
		return false;
	}

	out << root.dump(2);
	return true;
}

bool SceneSerializer::Load(Scene& scene, const std::string& filePath)
{
	std::ifstream in(filePath);
	if (!in)
	{
		return false;
	}

	json root;
	in >> root;

	if (!root.contains("entities"))
	{
		return false;
	}

	scene.ResetWorld();

	World& world = scene.GetWorld();
	PhysicsWorld* physicsWorld = nullptr;

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
			continue;
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
	}

	return true;
}