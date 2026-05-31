#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../include/dns_sd.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bsa {

struct TxtRecord; // forward from config.h

// ---------------------------------------------------------------------------
// A handle to a single active DNS-SD registration.
// Calling Deregister() or destroying the object removes the advertisement.
// ---------------------------------------------------------------------------
class Advertisement {
public:
    Advertisement() = default;
    ~Advertisement() { Deregister(); }

    Advertisement(const Advertisement&)            = delete;
    Advertisement& operator=(const Advertisement&) = delete;
    Advertisement(Advertisement&&)                 = default;
    Advertisement& operator=(Advertisement&&)      = default;

    bool IsRegistered() const { return m_sdRef != nullptr; }
    void Deregister();

    // Internal — set by BonjourPublisher
    DNSServiceRef             m_sdRef     = nullptr;
    DNSServiceRefDeallocate_f m_deallocFn = nullptr;
};

// ---------------------------------------------------------------------------
// BonjourPublisher
//
// Loads dnssd.dll dynamically.  If Bonjour is unavailable the publisher
// enters a degraded state and retries every 60 seconds.
// ---------------------------------------------------------------------------
class BonjourPublisher {
public:
    BonjourPublisher();
    ~BonjourPublisher();

    BonjourPublisher(const BonjourPublisher&)            = delete;
    BonjourPublisher& operator=(const BonjourPublisher&) = delete;

    /// Attempt to (re-)load dnssd.dll.  Returns true if Bonjour is ready.
    bool EnsureLoaded();

    /// Returns true when dnssd.dll is loaded and functional.
    bool IsAvailable() const;

    /// Publish a DNS-SD service advertisement.
    /// @param name     Human-readable service name (UTF-8).
    /// @param type     Service type e.g. "_ssh._tcp".
    /// @param port     TCP/UDP port in host byte order.
    /// @param txtRecords Optional TXT key=value pairs.
    /// @returns        Owning Advertisement handle, or empty on error.
    std::unique_ptr<Advertisement> Register(
        const std::string&               name,
        const std::string&               type,
        uint16_t                         port,
        const std::vector<TxtRecord>&    txtRecords = {});

private:
    bool LoadDll();
    void UnloadDll();

    mutable std::mutex m_mutex;
    HMODULE            m_hDll     = nullptr;

    // Resolved function pointers
    DNSServiceRegister_f      m_register      = nullptr;
    DNSServiceRefDeallocate_f m_deallocate    = nullptr;
    DNSServiceProcessResult_f m_processResult = nullptr;
    TXTRecordCreate_f         m_txtCreate     = nullptr;
    TXTRecordSetValue_f       m_txtSet        = nullptr;
    TXTRecordGetLength_f      m_txtLen        = nullptr;
    TXTRecordGetBytesPtr_f    m_txtBytes      = nullptr;
    TXTRecordDeallocate_f     m_txtDealloc    = nullptr;
};

} // namespace bsa
