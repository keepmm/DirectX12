#pragma once
#include "MonoBehavior.hpp"
#include "Logger.hpp"
#include "Components.hpp"

class NewScript : public MonoBehavior
{
public:
    void OnStart() override {}
    void OnUpdate(float dt) override
    {
        // auto& tr = transform();
        LOG->LogInfo("[NewScript] Update22");
    }
};
