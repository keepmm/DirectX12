#pragma once

#include "Defines.hpp"
#include <memory>

class Mesh;
class Material;

struct TransformComponent
{
	float4x4 world{};
};

struct SpinComponent
{
	float angle = 0.0f;
	float speed = 1.0f;
};

struct MeshComponent
{
	std::shared_ptr<Mesh> mesh;
};

struct MaterialComponent
{
	std::shared_ptr<Material> material;
};
