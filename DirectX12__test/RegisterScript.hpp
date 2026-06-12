#pragma once

#include "World.hpp"
#include <map>
#include <functional>

class RegisterScript
{
public:
	using Factory = std::function<void(World&, Entity)>;

	static RegisterScript& Get() { static RegisterScript instance; 	return instance; }

	void Register(
		_In_ const std::string& name,
		_In_ Factory factory
	);

	const std::map<std::string, Factory>& GetAll() const { return m_Map; }
	void Attach(
		_In_ const std::string& name,
		_In_ World& world,
		_In_ Entity entity
	)
	{
		auto it = m_Map.find(name);
		if (it != m_Map.end())
		{
			it->second(world, entity);
		}
	}

	std::vector<std::string> Names() const
	{
		std::vector<std::string> out;
		out.reserve(m_Map.size());
		for (auto& [name, _] : m_Map)
		{
			out.push_back(name);
		}
		return out;
	}
private:
	std::map<std::string, Factory> m_Map;
};


// 各スクリプトの末尾に書く登録マクロ
#define REGISTER_SCRIPT(TYPE)                                            \
    namespace {                                                          \
        struct TYPE##_Registrar {                                        \
            TYPE##_Registrar() {                                         \
                RegisterScript::Get().Register(#TYPE,                    \
                    [](World& w, Entity e){                              \
                        auto& sc = w.HasComponent<ScriptComponent>(e)    \
                            ? w.GetComponent<ScriptComponent>(e)         \
                            : w.AddComponent(e, ScriptComponent{});      \
                        sc.AddBehavior<TYPE>(w, e);                      \
                    });                                                  \
            }                                                            \
        } g_##TYPE##_registrar;                                          \
    }
