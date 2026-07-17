#pragma once

#include "Logger.hpp"

namespace Debug
{
    inline void (*g_LogInfo)(const char*) = nullptr;
    inline void (*g_LogWarning)(const char*) = nullptr;
    inline void (*g_LogError)(const char*) = nullptr;

    inline void Log(const std::string& msg)
    {
        if (g_LogInfo) g_LogInfo(msg.c_str());
    }
    inline void LogWarning(const std::string& msg)
    {
        if (g_LogWarning) g_LogWarning(msg.c_str());
    }
    inline void LogError(const std::string& msg)
    {
        if (g_LogError) g_LogError(msg.c_str());
    }

	template<typename... Args>
	inline std::string Concat(Args&&... args)
	{
		std::ostringstream oss;
		(oss << ... << args);
		return oss.str();
	}
}

