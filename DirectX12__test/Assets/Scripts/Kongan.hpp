#pragma once
#include "MonoBehavior.hpp"
#include "Input.hpp"
#include "GameAPI.hpp"

class Kongan : public MonoBehavior
{
public:
    float3 launchPos{ 0.0f, 0.0f, 0.0f };
    void RegisterFields() override
    {
        Field("LaunchPos", launchPos);
    }

    void OnUpdate(float dt) override
    {
        if (INPUT->Key.Space().Down())
        {
            // u8"" でUTF-8を保証(ExplodeText側がUTF-8前提のため)
            GameAPI::LaunchFirework(
                launchPos, GameAPI::Text,
                float3{ 1.0f, 0.85f, 0.2f },
                reinterpret_cast<const char*>(u8"5Aください"));
        }
    }
};