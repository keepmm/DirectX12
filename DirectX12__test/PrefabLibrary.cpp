#include "PrefabLibrary.hpp"
#include "Components.hpp"
#include "Systems.hpp"

std::string ToHex(std::size_t value)
{
	std::ostringstream stream;
	stream << std::hex << std::setw(16) << std::setfill('0') << value;
	return stream.str();
}

PrefabLibrary& PrefabLibrary::Get()
{
	static PrefabLibrary instance;
	return instance;
}

void PrefabLibrary::RegisterPrefab(const std::string& name, const std::string& guid, PrefabBuilder builder)
{
	m_Prefabs[name] = std::move(builder);
	m_PrefabGuids[name] = guid;
	m_GuidtoPrefab[guid] = name;
}

void PrefabLibrary::RegisterPrefab(const std::string& name, PrefabBuilder builder)
{
	std::string guid = MakeGuidFromName(name);
	RegisterPrefab(name, guid, std::move(builder));
}

bool PrefabLibrary::HasPrefab(const std::string& name) const
{
	return m_Prefabs.find(name) != m_Prefabs.end();
}

bool PrefabLibrary::HasPrefabGuid(const std::string& guid) const
{
	return m_GuidtoPrefab.find(guid) != m_GuidtoPrefab.end();
}

Entity PrefabLibrary::Instantiate(const std::string& name, Scene& scene,World& world) const
{
	auto it = m_Prefabs.find(name);
	if (it == m_Prefabs.end())
	{
		return INVALID_ENTITY;
	}

	Entity entity = world.CreateEntity();
	it->second(scene,world, entity);

	if (!world.HasComponent<NameComponent>(entity))
	{
		world.AddComponent<NameComponent>(entity, NameComponent{ name });
	}

	// ñºëOÇ©Ç‘ÇËÇëŒçÙ
	NameSytem::SetName(world, entity, name);

	world.AddComponent<PrefabComponent>(entity, PrefabComponent{ name });
	return entity;
}

Entity PrefabLibrary::InstantiateByGuid(const std::string& guid,Scene& scene, World& world) const
{
	const std::string name = GetPrefabGuid(guid);
	if (name.empty())
	{
		return INVALID_ENTITY;
	}

	return Instantiate(name, scene, world);
}

std::vector<std::string> PrefabLibrary::GetPrefabNames() const
{
	std::vector<std::string> names;
	names.reserve(m_Prefabs.size());
	for(const auto& [name, _] : m_Prefabs)
	{
		names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	return names;
}

std::string PrefabLibrary::GetPrefabGuid(const std::string& name) const
{
	auto it = m_PrefabGuids.find(name);
	if (it == m_PrefabGuids.end())
	{
		return {};
	}
	return it->second;
}

std::string PrefabLibrary::GetPrefabNameByGuid(const std::string& guid) const
{
	auto it = m_GuidtoPrefab.find(guid);
	if (it == m_GuidtoPrefab.end())
	{
		return {};
	}

	return it->second;
}

std::string PrefabLibrary::MakeGuidFromName(const std::string& name) const
{
	return ToHex(std::hash<std::string>{}(name));
}
