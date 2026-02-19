#pragma once

#include "Scene.hpp"
#include "Defines.hpp"
#include "Camera.hpp"

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
};

