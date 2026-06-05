// main.cpp — Windows Service entry point for BonjourServiceAdvertiser

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>     // SHGetFolderPathW / CSIDL_COMMON_APPDATA

#include "service_worker.h"
#include "logger.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static const wchar_t* kServiceName    = L"BonjourServiceAdvertiser";
static SERVICE_STATUS_HANDLE  g_hSS   = nullptr;
static SERVICE_STATUS         g_SS    = {};
static bsa::ServiceWorker*    g_worker = nullptr;

// ---------------------------------------------------------------------------
// SetServiceStatus helper
// ---------------------------------------------------------------------------
static void ReportStatus(DWORD state, DWORD exitCode = NO_ERROR,
                          DWORD waitHint = 0)
{
    static DWORD checkpoint = 1;
    g_SS.dwCurrentState  = state;
    g_SS.dwWin32ExitCode = exitCode;
    g_SS.dwWaitHint      = waitHint;
    g_SS.dwCheckPoint    =
        (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0
                                                               : checkpoint++;
    SetServiceStatus(g_hSS, &g_SS);
}

// ---------------------------------------------------------------------------
// Service control handler
// ---------------------------------------------------------------------------
static DWORD WINAPI ServiceCtrlHandler(DWORD control, DWORD /*eventType*/,
                                        LPVOID /*eventData*/, LPVOID /*ctx*/)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        if (g_worker) g_worker->StopAsync();
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// ---------------------------------------------------------------------------
// ServiceMain
// ---------------------------------------------------------------------------
static VOID WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    g_SS.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    g_SS.dwControlsAccepted        = SERVICE_ACCEPT_STOP |
                                     SERVICE_ACCEPT_SHUTDOWN;
    g_SS.dwCurrentState            = SERVICE_START_PENDING;
    g_SS.dwWin32ExitCode           = NO_ERROR;
    g_SS.dwCheckPoint              = 0;
    g_SS.dwWaitHint                = 5000;

    g_hSS = RegisterServiceCtrlHandlerExW(kServiceName,
                                           ServiceCtrlHandler, nullptr);
    if (!g_hSS) return;

    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 5000);

    bsa::ServiceWorker worker;
    g_worker = &worker;

    ReportStatus(SERVICE_RUNNING);

    worker.Run(); // blocks until StopAsync() is called

    g_worker = nullptr;
    ReportStatus(SERVICE_STOPPED);
}

// ---------------------------------------------------------------------------
// main
// When run as a service: dispatch to ServiceMain.
// When run interactively with --install / --uninstall: handle that.
// ---------------------------------------------------------------------------
static bool InstallService() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;

    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr,
                                    SC_MANAGER_CREATE_SERVICE);
    if (!hScm) return false;

    SC_HANDLE hSvc = CreateServiceW(
        hScm,
        kServiceName,
        L"Bonjour Service Advertiser",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        path,
        nullptr,  // load-order group
        nullptr,  // tag id
        nullptr,  // dependencies
        L"NT AUTHORITY\\LocalService",
        nullptr); // password

    bool ok = (hSvc != nullptr);
    if (ok) {
        // Set delayed auto-start to false (start immediately)
        SERVICE_DELAYED_AUTO_START_INFO dasi{ FALSE };
        ChangeServiceConfig2W(hSvc,
                              SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &dasi);

        // Set description
        SERVICE_DESCRIPTIONW desc{
            const_cast<LPWSTR>(
                L"Automatically advertises Windows services via Bonjour (mDNS/DNS-SD).")
        };
        ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &desc);

        // Set failure actions: restart on crash (3 times, 60s reset)
        SC_ACTION actions[3] = {
            { SC_ACTION_RESTART, 60000 },
            { SC_ACTION_RESTART, 60000 },
            { SC_ACTION_RESTART, 60000 }
        };
        SERVICE_FAILURE_ACTIONSW sfa{};
        sfa.dwResetPeriod = 86400; // 1 day
        sfa.cActions      = 3;
        sfa.lpsaActions   = actions;
        ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

        StartServiceW(hSvc, 0, nullptr);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hScm);
    return ok;
}

static bool UninstallService() {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr,
                                    SC_MANAGER_CONNECT);
    if (!hScm) return false;

    SC_HANDLE hSvc = OpenServiceW(hScm, kServiceName,
                                  SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!hSvc) { CloseServiceHandle(hScm); return false; }

    SERVICE_STATUS ss{};
    if (QueryServiceStatus(hSvc, &ss) &&
        ss.dwCurrentState != SERVICE_STOPPED)
    {
        ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
        Sleep(2000);
    }

    const bool ok = DeleteService(hSvc) != FALSE;
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    return ok;
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc > 1) {
        if (_wcsicmp(argv[1], L"--install") == 0) {
            return InstallService() ? 0 : 1;
        }
        if (_wcsicmp(argv[1], L"--uninstall") == 0) {
            return UninstallService() ? 0 : 1;
        }
        if (_wcsicmp(argv[1], L"--run-console") == 0) {
            // Run interactively for debugging
            bsa::ServiceWorker worker;
            worker.Run();
            return 0;
        }
    }

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr }
    };
    StartServiceCtrlDispatcherW(table);
    return 0;
}
