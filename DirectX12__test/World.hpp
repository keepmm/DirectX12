#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

using Entity = std::uint32_t;
constexpr Entity INVALID_ENTITY = 0;

class World
{
public:
	Entity CreateEntity()
	{
		return m_NextEntityId++;
	}

	template<typename T>
	T& AddComponent(Entity entity, T component)
	{
		auto& storage = GetOrCreateStorage<T>();
		storage.Data[entity] = std::move(component);
		return storage.Data[entity];
	}

	template<typename T>
	bool HasComponent(Entity entity) const
	{
		const auto* storage = TryGetStorage<T>();
		if (storage == nullptr) {
			return false;
		}
		return storage->Data.find(entity) != storage->Data.end();
	}

	template<typename T>
	T& GetComponent(Entity entity)
	{
		auto* storage = TryGetStorage<T>();
		return storage->Data.at(entity);
	}

	template<typename T>
	const T& GetComponent(Entity entity) const
	{
		const auto* storage = TryGetStorage<T>();
		return storage->Data.at(entity);
	}

	template<typename TFirst, typename... TRest, typename Func>
	void Each(Func&& func)
	{
		auto* firstStorage = TryGetStorage<TFirst>();
		if (firstStorage == nullptr) {
			return;
		}

		for (auto& [entity, firstComponent] : firstStorage->Data) {
			if ((HasComponent<TRest>(entity) && ...)) {
				func(entity, firstComponent, GetComponent<TRest>(entity)...);
			}
		}
	}

private:
	struct IStorage
	{
		virtual ~IStorage() = default;
	};

	template<typename T>
	struct Storage final : IStorage
	{
		std::unordered_map<Entity, T> Data;
	};

	template<typename T>
	Storage<T>& GetOrCreateStorage()
	{
		const auto key = std::type_index(typeid(T));
		auto it = m_Storages.find(key);
		if (it == m_Storages.end()) {
			auto created = std::make_unique<Storage<T>>();
			auto* raw = created.get();
			m_Storages[key] = std::move(created);
			return *raw;
		}

		return *static_cast<Storage<T>*>(it->second.get());
	}

	template<typename T>
	Storage<T>* TryGetStorage()
	{
		const auto key = std::type_index(typeid(T));
		auto it = m_Storages.find(key);
		if (it == m_Storages.end()) {
			return nullptr;
		}

		return static_cast<Storage<T>*>(it->second.get());
	}

	template<typename T>
	const Storage<T>* TryGetStorage() const
	{
		const auto key = std::type_index(typeid(T));
		auto it = m_Storages.find(key);
		if (it == m_Storages.end()) {
			return nullptr;
		}

		return static_cast<const Storage<T>*>(it->second.get());
	}

private:
	Entity m_NextEntityId = 1;
	std::unordered_map<std::type_index, std::unique_ptr<IStorage>> m_Storages;
};