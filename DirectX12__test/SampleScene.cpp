#include "SampleScene.hpp"
#include "Polygon.hpp"

SampleScene::~SampleScene()
{
}

SampleScene::SampleScene()
{
	m_Camera = MakeUnique<Camera>();
}

void SampleScene::Update()
{
	
}

void SampleScene::Draw()
{
	float4x4 world;
	DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixIdentity());
	static float angle = 0.0f;
	DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixRotationY(angle));
	angle += 0.01f;

	Polygon::SetWorld(world);
	Polygon::SetView(m_Camera->GetViewMatrix(false));
	Polygon::SetProjection(m_Camera->GetProjectionMatrix(false));
	Polygon::Draw();
}