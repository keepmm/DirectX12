/*****************************************************************//**
 * \file   Logger.hpp
 * \brief  ログ管理システム
 * 
 * 作成者 keepm
 * 作成日 2026/3/2
 * 更新履歴 2026/3/2: 新規作成
 * *********************************************************************/
#pragma once

#include <Windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#define LOG Logger::GetInstance()

enum class LogLevel : uint8_t
{
	Info,	// 通常情報
	Warning,// 警告
	Error,	// エラー
	Debug	// デバッグ
};

class Logger
{
public:
	static Logger* GetInstance();

	void Init(const std::string& logFilePath = "Game.log");
	void ShutDown();

	struct LogStream
	{
		Logger* logger;
		LogLevel level;
		std::ostringstream stream;

		LogStream(Logger* log, LogLevel lvl) : logger(log), level(lvl) {}

		~LogStream()
		{
			logger->WriteLog(level, stream.str());
		}

		template<typename T>
		LogStream& operator<<(const T& value)
		{
			stream << value;
			return *this;
		}
	};

	void LogHRESULT(HRESULT hr, const std::string& context);

	void LogInfo(const std::string& message);
	void LogWarning(const std::string& message);
	void LogError(const std::string& message);
	void LogDebug(const std::string& message);

private:
	Logger();
	~Logger();
	Logger(const Logger&) = delete;
	void operator=(const Logger&) = delete;

	void WriteLog(LogLevel level, const std::string& message);
	std::string GetCurrentTime() const;
	std::string GetLogLevelString(LogLevel level) const;

	std::ofstream m_LogFile;
	bool m_ConsoleOutput;
	bool m_FileOutput;
	bool m_Init;
};

// マクロ
#define LOG_INFO(msg) LOG->Info() << msg
#define LOG_WARNING(msg) LOG->Warning() << msg
#define LOG_ERROR(msg) LOG->Error() << msg
#define LOG_DEBUG(msg) LOG->Debug() << msg
#define LOG_HR(hr, context) LOG->LogHRESULT(hr, context)

