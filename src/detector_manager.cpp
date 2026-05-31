#include "detector_manager.h"
#include "logger.h"
#include "port_checker.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>

namespace bsa {

DetectorManager::DetectorManager()
    : m_detectors(CreateBuiltinDetectors())
{}

std::string DetectorManager::MakeKey(const std::string& type, uint16_t port) {
    return type + ":" + std::to_string(port);
}

// Get the computer name for use in service advertisements
static std::string GetComputerNameForAds() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buf, &size)) {
        // Convert wide to narrow and trim
        std::string name(buf, buf + size);
        // Replace spaces/underscores with hyphens for mDNS compatibility
        for (auto& c : name) {
            if (c == ' ' || c == '_') c = '-';
        }
        return name;
    }
    return "Computer";  // Fallback
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------
void DetectorManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active.clear(); // Advertisement destructors call Deregister()
}

// ---------------------------------------------------------------------------
// Reconcile
// ---------------------------------------------------------------------------
void DetectorManager::Reconcile(BonjourPublisher& publisher,
                                 const ServiceConfig& cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Get computer name to prepend to all service advertisements
    std::string computerName = GetComputerNameForAds();

    // -----------------------------------------------------------------------
    // 1. Collect the desired set of advertisements
    // -----------------------------------------------------------------------
    struct Desired {
        std::string            name;
        std::string            type;
        uint16_t               port;
        std::vector<TxtRecord> txt;
    };
    std::vector<Desired> desired;

    // Built-in detectors (gated by ServiceConfig)
    for (const auto& det : m_detectors) {
        // Check per-service enable flags
        const char* t = det->ServiceType();
        bool enabled = false;
        if      (cfg.ssh      && std::string(t) == "_ssh._tcp")   enabled = true;
        else if (cfg.smb      && std::string(t) == "_smb._tcp")   enabled = true;
        else if (cfg.rdp      && std::string(t) == "_rdp._tcp")   enabled = true;
        else if (cfg.http     && std::string(t) == "_http._tcp")
        {
            // HTTP and WebDAV share port 80 — let each decide
            enabled = true;
        }
        else if (cfg.https    && std::string(t) == "_https._tcp") enabled = true;
        else if (cfg.ftp      && std::string(t) == "_ftp._tcp")   enabled = true;
        else if (cfg.webdav   && std::string(t) == "_webdav._tcp") enabled = true;
        else if (cfg.printers && std::string(t) == "_ipp._tcp")   enabled = true;

        if (!enabled) continue;
        if (!det->IsActive()) continue;

        // Prepend computer name to service name (e.g., "MYPC-SSH")
        std::string fullName = computerName + "-" + det->Name();
        desired.push_back({fullName, t, det->Port(), det->TxtRecords()});
    }

    // Custom advertisements
    for (const auto& ad : cfg.customAds) {
        if (!ad.enabled) continue;
        if (!IsPortListening(ad.port)) continue;
        if (!HasNonLoopbackInterface()) continue;
        desired.push_back({ad.name, ad.type, ad.port, ad.txtRecords});
    }

    // -----------------------------------------------------------------------
    // 2. Remove registrations that are no longer desired
    // -----------------------------------------------------------------------
    for (auto it = m_active.begin(); it != m_active.end(); ) {
        const std::string& key = it->first;
        bool still_desired = false;
        for (const auto& d : desired) {
            if (MakeKey(d.type, d.port) == key) {
                still_desired = true;
                break;
            }
        }
        if (!still_desired) {
            Logger::Get().Info("reconcile", "Withdrawing advertisement",
                {{"key", key}});
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }

    // -----------------------------------------------------------------------
    // 3. Add new registrations
    // -----------------------------------------------------------------------
    for (const auto& d : desired) {
        const std::string key = MakeKey(d.type, d.port);
        if (m_active.count(key)) continue; // already registered

        auto adv = publisher.Register(d.name, d.type, d.port, d.txt);
        if (adv) {
            m_active.emplace(key, std::move(adv));
        }
    }
}

} // namespace bsa
