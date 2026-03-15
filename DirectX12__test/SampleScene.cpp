#include "SampleScene.hpp"
#include "Polygon.hpp"
#include <cmath>
#include "Time.hpp"
#include "Input.hpp"
#include "Components.hpp"

SampleScene::~SampleScene()
{
}

SampleScene::SampleScene()
{
	m_Camera = MakeUnique<Camera>();
	Polygon::DrawWireFrame(true);

	m_CubeEntity = m_World.CreateEntity();

	TransformComponent transform{};
	DirectX::XMStoreFloat4x4(&transform.world, DirectX::XMMatrixIdentity());
	m_World.AddComponent<TransformComponent>(m_CubeEntity, transform);

	SpinComponent spin{};
	spin.angle = 0.0f;
	spin.speed = 1.0f;
	m_World.AddComponent<SpinComponent>(m_CubeEntity, spin);
}

void SampleScene::Update()
{
	if (INPUT->Key.Escape().Down())
	{
		PostQuitMessage(0);
	}

	if (INPUT->Key.Enter().Down())
	{
		static bool wireframe = true;
		wireframe = !wireframe;
		Polygon::DrawWireFrame(wireframe);
	}

	if (INPUT->MouseInput.Left().Down())
	{
		static float4 color = { 1.0f, 0.0f, 0.0f, 1.0f };
		Polygon::SetLightColor(color);
	}

	float deltaTime = TIME->GetDeltaTime();
	m_SpinSystem.Update(m_World, deltaTime);

	static float lightAngle = 0.0f;
	lightAngle += 0.5f * deltaTime;

	const float3 dir = {
		std::cos(lightAngle),
		-0.5f,
		std::sin(lightAngle)
	};

	Polygon::SetLightDir(dir);

	static float4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	color.x = (std::cos(lightAngle) + 1.0f) * 0.5f;
	Polygon::SetLightColor(color);

	static float4 ambientColor = { 0.2f, 0.2f, 0.2f, 1.0f };
	static float ambientAngle = 0.0f;
	ambientAngle += 0.01f * deltaTime;
	ambientColor.x = (std::cos(ambientAngle) + 1.0f) * 0.5f;
	Polygon::SetAmbientColor(ambientColor);
}

void SampleScene::Draw()
{
	auto& transform = m_World.GetComponent<TransformComponent>(m_CubeEntity);

	Polygon::SetWorld(transform.world);
	Polygon::SetView(m_Camera->GetViewMatrix(false));
	Polygon::SetProjection(m_Camera->GetProjectionMatrix(false));
	Polygon::Draw();
}