#include "logger.h"

#include <cstdio>
#include <string>

namespace bsa {

// ---------------------------------------------------------------------------
// Singleton accessor
// ---------------------------------------------------------------------------
Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
Logger::~Logger() {
    if (m_hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
    if (m_hEventLog != nullptr) {
        DeregisterEventSource(m_hEventLog);
        m_hEventLog = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void Logger::Init(const std::wstring& logDir) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_logDir = logDir;

    // Create the log directory; ignore ERROR_ALREADY_EXISTS
    CreateDirectoryW(logDir.c_str(), nullptr);

    // Register event source (best-effort; continues even if it fails)
    m_hEventLog = RegisterEventSourceW(nullptr, L"BonjourServiceAdvertiser");

    OpenLogFile();
}

// ---------------------------------------------------------------------------
// Private: OpenLogFile
// Opens m_fileIndex for appending and records its current size.
// Caller must hold m_mutex.
// ---------------------------------------------------------------------------
void Logger::OpenLogFile() {
    if (m_hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }

    std::wstring path = m_logDir + L"\\advertiser_"
                      + std::to_wstring(m_fileIndex) + L".log";

    m_hFile = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz{};
        m_fileSize = GetFileSizeEx(m_hFile, &sz)
                     ? static_cast<uint64_t>(sz.QuadPart)
                     : 0;
    }
}

// ---------------------------------------------------------------------------
// Private: RollFile
// Advances to the next log slot (wrapping after 9) and truncates it.
// Caller must hold m_mutex.
// ---------------------------------------------------------------------------
void Logger::RollFile() {
    if (m_hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }

    m_fileIndex = (m_fileIndex + 1) % kMaxFiles;

    std::wstring path = m_logDir + L"\\advertiser_"
                      + std::to_wstring(m_fileIndex) + L".log";

    // CREATE_ALWAYS truncates any existing content in the slot
    m_hFile = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    m_fileSize = 0;
}

// ---------------------------------------------------------------------------
// Private: static helpers
// ---------------------------------------------------------------------------
std::string Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG_LEVEL:   return "DEBUG";
        case LogLevel::INFO:          return "INFO";
        case LogLevel::WARNING_LEVEL: return "WARNING";
        case LogLevel::ERR:           return "ERROR";
        case LogLevel::CRITICAL:      return "CRITICAL";
        default:                      return "UNKNOWN";
    }
}

std::string Logger::EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string Logger::GetIso8601Z() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth,  st.wDay,
             st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
    return buf;
}

// ---------------------------------------------------------------------------
// Private: WriteToEventLog
// Safe to call without m_mutex held (m_hEventLog is write-once after Init).
// ---------------------------------------------------------------------------
void Logger::WriteToEventLog(LogLevel level, const std::string& line) {
    if (!m_hEventLog) return;

    WORD type;
    switch (level) {
        case LogLevel::WARNING_LEVEL:
            type = EVENTLOG_WARNING_TYPE;
            break;
        case LogLevel::ERR:
        case LogLevel::CRITICAL:
            type = EVENTLOG_ERROR_TYPE;
            break;
        default:
            type = EVENTLOG_INFORMATION_TYPE;
            break;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                   line.c_str(), static_cast<int>(line.size()),
                                   nullptr, 0);
    if (wlen <= 0) return;

    std::wstring wmsg(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        line.c_str(), static_cast<int>(line.size()),
                        wmsg.data(), wlen);

    const WCHAR* strings[1] = { wmsg.c_str() };
    ReportEventW(m_hEventLog, type, 0, 0, nullptr, 1, 0, strings, nullptr);
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
void Logger::Log(LogLevel                           level,
                 const std::string&                 event,
                 const std::string&                 msg,
                 std::map<std::string, std::string> fields) {
    // Build the JSON line before acquiring the lock to minimise contention.
    const std::string ts       = GetIso8601Z();
    const std::string levelStr = LevelToString(level);

    std::string line;
    line.reserve(256);
    line += "{\"time\":\"";  line += EscapeJson(ts);
    line += "\",\"level\":\""; line += levelStr;
    line += "\",\"event\":\""; line += EscapeJson(event);
    line += "\",\"msg\":\"";   line += EscapeJson(msg);
    line += "\"";

    for (const auto& [k, v] : fields) {
        line += ",\"";
        line += EscapeJson(k);
        line += "\":\"";
        line += EscapeJson(v);
        line += "\"";
    }
    line += "}\n";

    // Write to file under lock
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_hFile != INVALID_HANDLE_VALUE) {
            if (m_fileSize + line.size() > kMaxFileSize) {
                RollFile();
            }

            if (m_hFile != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(m_hFile,
                          line.data(),
                          static_cast<DWORD>(line.size()),
                          &written,
                          nullptr);
                m_fileSize += written;
            }
        }
    }

    // Event log write is outside the lock (ReportEventW is thread-safe)
    WriteToEventLog(level, line);
}

// ---------------------------------------------------------------------------
// Convenience wrappers
// ---------------------------------------------------------------------------
void Logger::Debug(const std::string& event, const std::string& msg,
                   std::map<std::string, std::string> fields) {
    Log(LogLevel::DEBUG_LEVEL, event, msg, std::move(fields));
}

void Logger::Info(const std::string& event, const std::string& msg,
                  std::map<std::string, std::string> fields) {
    Log(LogLevel::INFO, event, msg, std::move(fields));
}

void Logger::Warn(const std::string& event, const std::string& msg,
                  std::map<std::string, std::string> fields) {
    Log(LogLevel::WARNING_LEVEL, event, msg, std::move(fields));
}

void Logger::Error(const std::string& event, const std::string& msg,
                   std::map<std::string, std::string> fields) {
    Log(LogLevel::ERR, event, msg, std::move(fields));
}

} // namespace bsa
