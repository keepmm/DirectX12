#pragma once
#include "MonoBehavior.hpp"
#include "Logger.hpp"
#include "Components.hpp"
#include "PlayState.hpp"

class kawaguchi : public MonoBehavior
{
public:
    bool os;
    int wa = 10;
    void RegisterFields() override
    {
        Field("os", os);
        Field("wa", wa);
	}
    //SERIALIZE_FIELD(int, "wawawa", 10);
    void OnStart() override {}
    void OnUpdate(float dt) override
    {
        auto& tr = transform();
        tr.Translate(0.1f, 0.1f, 0.1f);
    }
};
