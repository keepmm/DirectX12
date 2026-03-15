#pragma once

#include "Scene.hpp"
#include "Defines.hpp"
#include "Camera.hpp"
#include "World.hpp"
#include "Systems.hpp"

class SampleScene :
	public Scene
{
public:
	SampleScene();
	~SampleScene() final;

	void Update() final;
	void Draw() final;
private:
	std::unique_ptr<Camera> m_Camera;

	World m_World;
	Entity m_CubeEntity = INVALID_ENTITY;

	SpinSystem m_SpinSystem;
};