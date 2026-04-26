#pragma once

#include "RenderContext.hpp"

class Scene
{
	public:
	Scene() = default;
	virtual ~Scene() {};
	virtual void Update() = 0;
	virtual void Draw(_In_ const RenderContext& renderContext) = 0;
};

