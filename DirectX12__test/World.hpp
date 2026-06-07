/*****************************************************************//**
 * \file   World.hpp
 * \brief  Scene内のEntityとComponentを管理するクラス
 * 
 * 作成者 keeeep
 * 作成日 2026/2/16
 * 更新履歴	2.16 作成
 *			5.25 Entity削除処理を追加
 * *********************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

using Entity = std::uint32_t;
constexpr Entity INVALID_ENTITY = 0;

class World
{
public:
	Entity CreateEntity()
	{
		Entity entity = m_NextEntityId++;
		m_Entities.push_back(entity);
		return entity;
	}

	void DestroyEntity(Entity entity)
	{
		// 引数で受け取ったEntityが存在するか確認
		auto it = std::find(m_Entities.begin(), m_Entities.end(), entity);
		// 存在しない場合はこれ以上処理しない
		if (it == m_Entities.end())
		{
			return;
		}

		// 存在する場合はEntityを削除
		m_Entities.erase(it);
		for (auto& [_, storage] : m_Storages)
		{
			storage->Remove(entity);
		}
	}

	template<typename T>
	T& AddComponent(Entity entity, T component)
	{
		auto& storage = GetOrCreateStorage<T>();
		storage.Data[entity] = std::move(component);
		return storage.Data[entity];
	}

	template<typename T>
	void DeleteComponent(Entity entity)
	{
		auto& storage = GetOrCreateStorage<T>();
		storage.Remove(entity);
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

	const std::vector<Entity>& GetEntities() const
	{
		return m_Entities;
	}

	bool IsEntityAlive(Entity entity) const
	{
		return std::find(m_Entities.begin(), m_Entities.end(), entity) != m_Entities.end();
	}

	void Clear()
	{
		m_Entities.clear();
		m_Storages.clear();
		m_NextEntityId = 1;
	}

private:
	struct IStorage
	{
		virtual ~IStorage() = default;
		virtual void Remove(Entity entity) = 0;
	};

	template<typename T>
	struct Storage final : IStorage
	{
		std::unordered_map<Entity, T> Data;

		void Remove(Entity entity) override
		{
			Data.erase(entity);
		}
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
	std::vector<Entity> m_Entities;
	std::unordered_map<std::type_index, std::unique_ptr<IStorage>> m_Storages;
};