#include "Scene.hpp"

PhysicsWorld& Scene::EnsurePhysicsWorld()
{
    if (!m_PhysicsWorld)
    {
        m_PhysicsWorld = std::make_unique<PhysicsWorld>();
    }
    return *m_PhysicsWorld;
}

bool Scene::HasPhysicsWorld() const
{
    return m_PhysicsWorld != nullptr;
}

void Scene::ResetWorld()
{
	m_World.Clear();
    ResetPhysicsWorld();
}

void Scene::ResetPhysicsWorld()
{
    m_PhysicsWorld.reset();
}

Scene::Scene()
{
}
