#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace bsa {

enum class LogLevel {
    DEBUG_LEVEL,
    INFO,
    WARNING_LEVEL,
    ERR,
    CRITICAL
};

class Logger {
public:
    static Logger& Get();

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    void Init(const std::wstring& logDir);

    void Log(LogLevel                           level,
             const std::string&                 event,
             const std::string&                 msg,
             std::map<std::string, std::string> fields = {});

    void Debug(const std::string& event, const std::string& msg,
               std::map<std::string, std::string> fields = {});
    void Info (const std::string& event, const std::string& msg,
               std::map<std::string, std::string> fields = {});
    void Warn (const std::string& event, const std::string& msg,
               std::map<std::string, std::string> fields = {});
    void Error(const std::string& event, const std::string& msg,
               std::map<std::string, std::string> fields = {});

private:
    Logger()  = default;
    ~Logger();

    void        OpenLogFile();
    void        RollFile();
    void        WriteToEventLog(LogLevel level, const std::string& line);

    static std::string LevelToString(LogLevel level);
    static std::string EscapeJson(const std::string& s);
    static std::string GetIso8601Z();

    mutable std::mutex m_mutex;
    HANDLE             m_hFile     = INVALID_HANDLE_VALUE;
    HANDLE             m_hEventLog = nullptr;
    std::wstring       m_logDir;
    int                m_fileIndex = 0;
    uint64_t           m_fileSize  = 0;

    static constexpr uint64_t kMaxFileSize = 50ULL * 1024 * 1024; // 50 MB
    static constexpr int      kMaxFiles    = 10;
};

} // namespace bsa
