#pragma once
#include "MonoBehavior.hpp"
#include "Logger.hpp"
#include "Components.hpp"

class Testtext : public MonoBehavior
{
public:
    EntityRef text;
    void RegisterFields() override
    {
        Field("text", text);
    }

    void OnStart() override {}
    void OnUpdate(float dt) override
    {
        World& w = GetWorld();

        if (w.HasComponent<UITextComponent>(text.id))
        {
			static float time = 0.0f;
			time += dt;
            auto& t = w.GetComponent<UITextComponent>(text.id);
            t.text = "Score" + std::to_string(time);
        }
    }
};
