#include "service_worker.h"
#include "bonjour_publisher.h"
#include "config.h"
#include "detector_manager.h"
#include "logger.h"
#include "port_checker.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <cstring>

#include <array>
#include <string>

namespace bsa {

// ---------------------------------------------------------------------------
// Config path helper
// ---------------------------------------------------------------------------
static std::wstring GetConfigPath() {
    wchar_t programData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA,
                                nullptr, 0, programData)))
    {
        // Fallback
        GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH);
    }
    return std::wstring(programData) +
           L"\\BonjourServiceAdvertiser\\advertiser.ini";
}

static std::wstring GetLogDir() {
    wchar_t programData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA,
                                nullptr, 0, programData)))
    {
        GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH);
    }
    return std::wstring(programData) + L"\\BonjourServiceAdvertiser\\logs";
}

// ---------------------------------------------------------------------------
// StopAsync
// ---------------------------------------------------------------------------
void ServiceWorker::StopAsync() {
    if (m_hStopEvent) SetEvent(m_hStopEvent);
}

// ---------------------------------------------------------------------------
// DoReconcile — reload config + reconcile advertisements
// ---------------------------------------------------------------------------
void ServiceWorker::DoReconcile() {
    static BonjourPublisher publisher;
    static DetectorManager  manager;

    if (!publisher.IsAvailable()) {
        publisher.EnsureLoaded();
    }

    if (!publisher.IsAvailable()) {
        Logger::Get().Warn("reconcile",
            "Bonjour unavailable — skipping reconciliation");
        return;
    }

    Config::Instance().Load(Config::Instance().GetPath());
    manager.Reconcile(publisher, Config::Instance().Get());
}

// ---------------------------------------------------------------------------
// SetupWatchers
// ---------------------------------------------------------------------------
void ServiceWorker::SetupWatchers() {
    // ----- Config file change notification -----
    const std::wstring configPath = GetConfigPath();
    // Watch the directory containing the config file
    const std::wstring configDir  = [&] {
        auto p = configPath;
        const auto slash = p.find_last_of(L"\\/");
        return (slash != std::wstring::npos) ? p.substr(0, slash) : p;
    }();

    // Ensure directory exists
    CreateDirectoryW(configDir.c_str(), nullptr);

    m_hConfigDir = CreateFileW(
        configDir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    m_hConfigChange = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // ----- Periodic timer (300 s) -----
    m_hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (m_hTimer) {
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -300LL * 10000000LL; // 300 s in 100-ns intervals
        SetWaitableTimer(m_hTimer, &dueTime,
                         300 * 1000, // period in ms
                         nullptr, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// TeardownWatchers
// ---------------------------------------------------------------------------
void ServiceWorker::TeardownWatchers() {
    if (m_hTimer) {
        CancelWaitableTimer(m_hTimer);
        CloseHandle(m_hTimer);
        m_hTimer = nullptr;
    }
    if (m_hConfigDir != INVALID_HANDLE_VALUE && m_hConfigDir) {
        CloseHandle(m_hConfigDir);
        m_hConfigDir = nullptr;
    }
    if (m_hConfigChange) {
        CloseHandle(m_hConfigChange);
        m_hConfigChange = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------
void ServiceWorker::Run() {
    // Init logger
    Logger::Get().Init(GetLogDir());
    Logger::Get().Info("service", "BonjourServiceAdvertiser starting");

    // Ensure config directory exists and load initial config
    const std::wstring configPath = GetConfigPath();
    {
        std::wstring dir = configPath;
        const auto slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir = dir.substr(0, slash);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    Config::Instance().Load(configPath);

    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetupWatchers();

    // Attempt initial Bonjour load + reconcile
    DoReconcile();

    // ----- Overlapped buffer for ReadDirectoryChangesW -----
    constexpr DWORD kDirBuf = 4096;
    alignas(DWORD) BYTE dirBuf[kDirBuf] = {};
    OVERLAPPED ov{};
    ov.hEvent = m_hConfigChange;

    auto postDirWatch = [&]() {
        if (m_hConfigDir && m_hConfigDir != INVALID_HANDLE_VALUE) {
            ResetEvent(m_hConfigChange);
            ReadDirectoryChangesW(
                m_hConfigDir, dirBuf, kDirBuf, FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                nullptr, &ov, nullptr);
        }
    };
    postDirWatch();

    // ----- Retry timer for Bonjour (60 s when unavailable) -----
    HANDLE hBonjourRetry = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    auto scheduleRetry = [&]() {
        if (hBonjourRetry) {
            LARGE_INTEGER due;
            due.QuadPart = -60LL * 10000000LL;
            SetWaitableTimer(hBonjourRetry, &due, 0, nullptr, nullptr, FALSE);
        }
    };

    // -----------------------------------------------------------------------
    // Main event loop
    // -----------------------------------------------------------------------
    std::array<HANDLE, 4> handles = {
        m_hStopEvent,
        m_hConfigChange,
        m_hTimer,
        hBonjourRetry
    };

    Logger::Get().Info("service", "Service running");

    while (true) {
        const DWORD idx = WaitForMultipleObjects(
            static_cast<DWORD>(handles.size()),
            handles.data(),
            FALSE,
            INFINITE);

        if (idx == WAIT_OBJECT_0) {
            // Stop requested
            break;
        }
        else if (idx == WAIT_OBJECT_0 + 1) {
            // Config file changed
            Logger::Get().Info("config", "Config file changed — reloading");
            DoReconcile();
            postDirWatch();
        }
        else if (idx == WAIT_OBJECT_0 + 2) {
            // Periodic 300-second reconciliation
            Logger::Get().Debug("reconcile", "Periodic reconciliation");
            DoReconcile();
        }
        else if (idx == WAIT_OBJECT_0 + 3) {
            // Bonjour retry timer fired
            Logger::Get().Info("bonjour", "Retrying Bonjour load");
            DoReconcile();
            // Keep retrying if still unavailable
            // (timer is one-shot; reschedule if needed)
        }
        else {
            // Unexpected result — brief sleep to avoid spin
            Sleep(500);
        }

        // If Bonjour is still unavailable after a reconcile attempt, schedule retry
        // (hBonjourRetry is one-shot; reschedule so it fires again in 60 s)
        if (!m_hStopEvent || WaitForSingleObject(m_hStopEvent, 0) != WAIT_OBJECT_0) {
            scheduleRetry();
        }
    }

    // Cleanup
    if (hBonjourRetry) {
        CancelWaitableTimer(hBonjourRetry);
        CloseHandle(hBonjourRetry);
    }

    TeardownWatchers();
    if (m_hStopEvent) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
    }

    Logger::Get().Info("service", "BonjourServiceAdvertiser stopped");
}

} // namespace bsa
