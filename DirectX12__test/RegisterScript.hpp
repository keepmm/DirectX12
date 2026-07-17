#pragma once

#include <map>
#include <string>
#include "MonoBehavior.hpp"

class RegisterScript {
    using Factory = std::function<std::unique_ptr<MonoBehavior>()>;
    std::map<std::string, Factory> m_Map;
public:
    static RegisterScript& Get() { static RegisterScript i; return i; }
    void Register(const std::string& n, Factory f) { m_Map[n] = std::move(f); }
    std::unique_ptr<MonoBehavior> Create(const std::string& n) {
        auto it = m_Map.find(n); return it != m_Map.end() ? it->second() : nullptr;
    }
    std::vector<std::string> Names() const { std::vector<std::string> names;
        for (const auto& [name, _] : m_Map) { names.push_back(name); }
        return names;
	}
};

#define REGISTER_SCRIPT(TYPE)                                   \
    namespace {                                                 \
        struct TYPE##_Reg {                                     \
            TYPE##_Reg(){ RegisterScript::Get().Register(#TYPE, \
                []{ return std::make_unique<TYPE>(); }); }      \
        } g_##TYPE##_reg;                                       \
    }