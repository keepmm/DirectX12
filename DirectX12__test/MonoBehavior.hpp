#pragma once

#include "RenderContext.hpp"
#include "World.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "Components.hpp"

class MonoBehavior
{
public:
	MonoBehavior() = default;
	virtual ~MonoBehavior() = default;
	virtual void OnStart() {}
	virtual void OnUpdate(_In_ float deltatime) {}
	virtual void OnFixedUpdate(_In_ float deltatime) {}
	virtual void OnLateUpdate(_In_ float deltatime) {}
	virtual void OnDraw(_In_ const RenderContext& context) {}

	template<typename T>
	T& GetComponent()
	{
		return m_World->GetComponent<T>(m_Entity);
	}

	template<typename T>
	bool HasComponent() const
	{
		return m_World->HasComponent<T>(m_Entity);
	}

	Entity GetEntity() const { return m_Entity; }
	World& GetWorld() const { return *m_World; }

	TransformComponent& transform() { return GetComponent<TransformComponent>(); }

	template <typename T>
	T& AddComponent(const T& component)
	{
		return m_World->AddComponent<T>(m_Entity, component);
	}

private:
	friend struct ScriptComponent;
	void Attach(World* world, Entity entity)
	{
		m_World = world;
		m_Entity = entity;
	}

	World* m_World = nullptr;
	Entity m_Entity = INVALID_ENTITY;
};

