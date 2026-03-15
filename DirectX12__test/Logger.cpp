#include "Logger.hpp"
#include <iostream>

Logger* Logger::GetInstance()
{
    static Logger instance;
    return &instance;
}

void Logger::WriteLog(LogLevel level, const std::string& message)
{
    // 初期化がされていない場合これ以上処理しない
    if (!m_Init) return;

	std::string TimeStamp = GetCurrentTime();
	std::string LevelStr = GetLogLevelString(level);
    std::string FullMessage = "[" + TimeStamp + "] [" + LevelStr + "] " + message;

    // コンソール出力
    if (m_ConsoleOutput)
    {
        // レベルに応じて色を変える(Windowsコンソール)
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        switch (level)
        {
        case LogLevel::Info:
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            break;
        case LogLevel::Warning:
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            break;
        case LogLevel::Error:
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
            break;
        case LogLevel::Debug:
            SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            break;
        }

		std::cout << FullMessage << std::endl;
        // 色を元に戻す
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    // ファイル出力
    if(m_FileOutput && m_LogFile.is_open())
    {
        m_LogFile << FullMessage << std::endl;
		m_LogFile.flush(); // すぐに書き込む
	}
}

std::string Logger::GetCurrentTime() const
{
    return std::string();
}

std::string Logger::GetLogLevelString(LogLevel level) const
{
    return std::string();
}

void Logger::Init(const std::string& logFilePath)
{
    if (m_Init) return;

    if (m_FileOutput)
    {
        m_LogFile.open(logFilePath, std::ios::out | std::ios::trunc);
        if(m_LogFile.is_open())
        {
            m_LogFile << "=============================================================" << std::endl;
			m_LogFile << "Log Start: " << GetCurrentTime() << std::endl;
            m_LogFile << "=============================================================" << std::endl;
        }

        // デバッグビルド時はコンソールにも出力
#ifdef _DEBUG
        AllocConsole();
        FILE* fp;
		freopen_s(&fp, "CONOUT$", "w", stdout);
		freopen_s(&fp, "CONOUT$", "w", stderr);
		std::cout << "=============================================================" << std::endl;
        std::cout << "Log Start: " << GetCurrentTime() << std::endl;
		std::cout << "=============================================================" << std::endl;
#endif

		m_Init = true;
		LogInfo("Logger initialized. Log file: " + logFilePath);
    }
}

void Logger::ShutDown()
{
    if (!m_Init) return;

	LogInfo("Logger shutting down.");

    if (m_LogFile.is_open())
    {
        m_LogFile << "=============================================================" << std::endl;
		m_LogFile << "Log End: " << GetCurrentTime() << std::endl;
        m_LogFile << "=============================================================" << std::endl;
		m_LogFile.close();
    }
#ifdef _DEBUG
	FreeConsole();
#endif
	m_Init = false;
}

void Logger::LogHRESULT(HRESULT hr, const std::string& context)
{
}

void Logger::LogInfo(const std::string& message)
{
	WriteLog(LogLevel::Info, message);
}

void Logger::LogWarning(const std::string& message)
{
	WriteLog(LogLevel::Warning, message);
}

void Logger::LogError(const std::string& message)
{
	WriteLog(LogLevel::Error, message);
}

void Logger::LogDebug(const std::string& message)
{
	WriteLog(LogLevel::Debug, message);
}

Logger::~Logger()
{
    ShutDown();
}

Logger::Logger()
{

}
