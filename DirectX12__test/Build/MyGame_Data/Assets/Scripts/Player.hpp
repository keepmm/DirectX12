#pragma once
#include "MonoBehavior.hpp"
#include "Logger.hpp"
#include "Components.hpp"
#include "Debug.hpp"
#include "Input.hpp"

class Player : public MonoBehavior
{
public:
    float speed = 1.0f;
    void RegisterFields() override
    {
        Field("Speed", speed);
    }
    void OnStart() override
    {
		Debug::Log("Player Start");
    }
    void OnUpdate(float dt) override
    {
        auto& tr = transform();

        float3 move{ 0.0f,0.0f,0.0f };
        if (INPUT->Key.W().IsPressed()) move.z += 1.0f;
        if (INPUT->Key.S().IsPressed()) move.z -= 1.0f;
        if (INPUT->Key.A().IsPressed()) move.x -= 1.0f;
        if (INPUT->Key.D().IsPressed()) move.x += 1.0f;

        tr.Translate(move.x * speed * dt, 0.0f, move.z * speed * dt);
    }
};
