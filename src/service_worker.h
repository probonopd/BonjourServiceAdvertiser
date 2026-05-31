#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace bsa {

// ---------------------------------------------------------------------------
// ServiceWorker
//
// Runs the main service logic on the calling thread.
// Blocks until StopAsync() is called (typically from the SCM control handler).
// ---------------------------------------------------------------------------
class ServiceWorker {
public:
    ServiceWorker() = default;
    ~ServiceWorker() = default;

    ServiceWorker(const ServiceWorker&)            = delete;
    ServiceWorker& operator=(const ServiceWorker&) = delete;

    /// Blocking entry point — called from ServiceMain after setup.
    void Run();

    /// Signal the worker to exit.  Safe to call from any thread.
    void StopAsync();

private:
    void SetupWatchers();
    void TeardownWatchers();
    void TryLoadBonjour();
    void DoReconcile();

    HANDLE m_hStopEvent    = nullptr;   // manual-reset; set by StopAsync()
    HANDLE m_hConfigChange = nullptr;   // ReadDirectoryChangesW notification
    HANDLE m_hTimer        = nullptr;   // waitable timer (300 s)
    HANDLE m_hConfigDir    = nullptr;   // directory handle for config watching
};

} // namespace bsa
