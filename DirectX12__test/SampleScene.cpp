#include "SampleScene.hpp"
#include "Polygon.hpp"
#include <cmath>
#include "Time.hpp"
#include "Input.hpp"
#include "Time.hpp"

SampleScene::~SampleScene()
{
}

SampleScene::SampleScene()
{
	m_Camera = MakeUnique<Camera>();
	Polygon::DrawWireFrame(true);
}

void SampleScene::Update()
{
	// カメラ更新

	// ESCキーで終了 - メソッドチェーン形式!
	if (INPUT->Key.Escape().Down())  // ← .Down() で押した瞬間を判定!
	{
		PostQuitMessage(0);
	}

	// Enterキーでワイヤーフレーム切り替え
	if (INPUT->Key.Enter().Down())
	{
		static bool wireframe = true;
		wireframe = !wireframe;
		Polygon::DrawWireFrame(wireframe);
	}

	// マウス左クリックでライトの色変更
	if (INPUT->MouseInput.Left().Down())
	{
		static float4 color = { 1.0f, 0.0f, 0.0f, 1.0f };
		Polygon::SetLightColor(color);
	}

	float deltaTime = TIME->GetDeltaTime();

	static float angle = 0.0f;
	angle += 0.5f * deltaTime;

	const float3 dir = {
		std::cos(angle),
		-0.5f,
		std::sin(angle)
	};

	Polygon::SetLightDir(dir);

	static float4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	color.x = (std::cos(angle) + 1.0f) * 0.5f;
	Polygon::SetLightColor(color);

	static float4 ambientColor = { 0.2f, 0.2f, 0.2f, 1.0f };
	static float ambientAngle = 0.0f;
	ambientAngle += 0.01f * deltaTime;
	ambientColor.x = (std::cos(ambientAngle) + 1.0f) * 0.5f;
	Polygon::SetAmbientColor(ambientColor);
}

void SampleScene::Draw()
{
	matrix world = DirectX::XMMatrixIdentity();
	static float angle = 0.0f;
	matrix r = DirectX::XMMatrixRotationY(angle);
	angle += 0.01f;
	world = r;

	float4x4 w;
	DirectX::XMStoreFloat4x4(&w, world);

	Polygon::SetWorld(w);
	Polygon::SetView(m_Camera->GetViewMatrix(false));
	Polygon::SetProjection(m_Camera->GetProjectionMatrix(false));
	Polygon::Draw();
}