#include "Actor.hpp"

Actor::Actor(World& world)
	: m_World(world),
	m_Entity(world.CreateEntity())
{
}

Actor::~Actor() = default;

void Actor::Start()
{
	if (m_Start) return;

	m_Start = true;
	for (auto& b : m_Behaviors)
	{
		b->OnStart();
	}
}

void Actor::Update(float deltatime)
{
	for (auto& b : m_Behaviors)
	{
		b->OnUpdate(deltatime);
	}
}

void Actor::FixedUpdate(float deltatime)
{
	for (auto& b : m_Behaviors)
	{
		b->OnFixedUpdate(deltatime);
	}
}

void Actor::LateUpdate(float deltatime)
{
	for (auto& b : m_Behaviors)
	{
		b->OnLateUpdate(deltatime);
	}
}

void Actor::Draw(const RenderContext& context)
{
	for (auto& b : m_Behaviors)
	{
		b->OnDraw(context);
	}
}