#pragma once

#include <string>
#include <vector>
#include <functional>
#include "world.hpp"
#include "SceneSerializer.hpp"
#include "json.hpp"
#include "ScriptField.hpp"

struct ComponentMeta
{
    std::string name;
    std::function<bool(World&, Entity)> has;
    std::function<void(World&, Entity)> add;
    std::function<void(World&, Entity)> draw;
    std::function<void(World&, Entity, nlohmann::json&)> save;
    std::function<void(World&, Entity, const nlohmann::json&)> load;
    std::function<void(World&, Entity, FieldList&)> reflect;
    /// @brief コンポーネントを外す(Undo でコンポーネントの追加を取り消すとき用)
    std::function<void(World&, Entity)> remove;
};

class ComponentRegistry
{
public:
    static const std::vector<ComponentMeta>& All();
};

