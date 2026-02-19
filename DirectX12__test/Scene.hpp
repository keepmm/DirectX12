#pragma once
class Scene
{
	public:
	Scene() = default;
	virtual ~Scene() {};
	virtual void Update() = 0;
	virtual void Draw() = 0;
};

