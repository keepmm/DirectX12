/*****************************************************************//**
 * \file   Actor.hpp
 * \brief  Actorクラスの定義
 *
 * 作成者
 * 作成日 2026/4/26
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "Defines.hpp"
#include "World.hpp"
#include "MonoBehavior.hpp"

class Actor
{
public:
	explicit Actor(_In_ World& world);
	~Actor();

	Actor(const Actor&) = delete;
	Actor& operator=(const Actor&) = delete;

	Entity GetEntity() const { return m_Entity; }

	World& GetWorld() { return m_World; }
	const World& GetWorld() const { return m_World; }

	template<typename T>
	T& AddComponent(T component)
	{
		return m_World.AddComponent<T>(m_Entity, std::move(component));
	}

	template<typename T>
	bool HasComponent() const
	{
		return m_World.HasComponent<T>(m_Entity);
	}

	template<typename T>
	T& GetComponent()
	{
		return m_World.GetComponent<T>(m_Entity);
	}

	template<typename T>
	const T& GetComponent() const
	{
		return m_World.GetComponent<T>(m_Entity);
	}

	template<typename TBehavior, typename... TArgs>
	TBehavior& AddBehavior(TArgs&&... args)
	{
		static_assert(std::is_base_of_v<MonoBehavior, TBehavior>, "TBehavior must derive from MonoBehavior");

		auto behavior = std::make_unique<TBehavior>(std::forward<TArgs>(args)...);
		auto* raw = behavior.get();
		raw->AttachActor(this);

		if (m_Start)
		{
			raw->OnStart();
		}

		m_Behaviors.emplace_back(std::move(behavior));
		return *raw;
	}

	void Start();
	void Update(_In_ float deltatime);
	void FixedUpdate(_In_ float deltatime);
	void LateUpdate(_In_ float deltatime);
	void Draw(_In_ const RenderContext& context);

private:
	World& m_World;
	Entity m_Entity;
	bool m_Start = false;

	std::vector<std::unique_ptr<MonoBehavior>> m_Behaviors;
};