#pragma once

#include <string>
#include <vector>
#include <functional>
#include "world.hpp"
#include "SceneSerializer.hpp"
#include "json.hpp"

struct ComponentMeta
{
    std::string name;
    std::function<bool(World&, Entity)> has;
    std::function<void(World&, Entity)> add;
    std::function<void(World&, Entity)> draw;
    std::function<void(World&, Entity, nlohmann::json&)> save;
    std::function<void(World&, Entity, const nlohmann::json&)> load;
};

class ComponentRegistry
{
public:
    static const std::vector<ComponentMeta>& All();
};

