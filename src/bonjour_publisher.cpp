#include "bonjour_publisher.h"
#include "config.h"
#include "logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#include <cstring>

namespace bsa {

// ---------------------------------------------------------------------------
// Advertisement
// ---------------------------------------------------------------------------
void Advertisement::Deregister() {
    if (m_sdRef && m_deallocFn) {
        m_deallocFn(m_sdRef);
        m_sdRef = nullptr;
    }
}

// ---------------------------------------------------------------------------
// BonjourPublisher
// ---------------------------------------------------------------------------
BonjourPublisher::BonjourPublisher() = default;

BonjourPublisher::~BonjourPublisher() {
    UnloadDll();
}

void BonjourPublisher::UnloadDll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hDll) {
        FreeLibrary(m_hDll);
        m_hDll          = nullptr;
        m_register      = nullptr;
        m_deallocate    = nullptr;
        m_processResult = nullptr;
        m_txtCreate     = nullptr;
        m_txtSet        = nullptr;
        m_txtLen        = nullptr;
        m_txtBytes      = nullptr;
        m_txtDealloc    = nullptr;
    }
}

// ---------------------------------------------------------------------------
// LoadDll  (must be called with m_mutex held)
// ---------------------------------------------------------------------------
bool BonjourPublisher::LoadDll() {
    if (m_hDll) return true; // already loaded

    // dnssd.dll ships with the Bonjour runtime installer
    m_hDll = LoadLibraryW(L"dnssd.dll");
    if (!m_hDll) {
        Logger::Get().Warn("bonjour", "dnssd.dll not found — Bonjour unavailable");
        return false;
    }

// Helper: load one symbol, assigning to a named member variable.
// FARPROC -> typed fn-ptr via void* intermediate (avoids -Wcast-function-type).
#define LOAD_SYM(member, type, symbol)                          \
    {                                                            \
        void* _p = reinterpret_cast<void*>(                     \
                       GetProcAddress(m_hDll, #symbol));        \
        if (!_p) {                                               \
            Logger::Get().Error("bonjour",                      \
                "Missing symbol: " #symbol);                    \
            FreeLibrary(m_hDll);                                \
            m_hDll = nullptr;                                   \
            return false;                                       \
        }                                                        \
        memcpy(&(member), &_p, sizeof(_p));                      \
    }

    LOAD_SYM(m_register,      DNSServiceRegister_f,      DNSServiceRegister)
    LOAD_SYM(m_deallocate,    DNSServiceRefDeallocate_f, DNSServiceRefDeallocate)
    LOAD_SYM(m_processResult, DNSServiceProcessResult_f, DNSServiceProcessResult)
    LOAD_SYM(m_txtCreate,     TXTRecordCreate_f,         TXTRecordCreate)
    LOAD_SYM(m_txtSet,        TXTRecordSetValue_f,       TXTRecordSetValue)
    LOAD_SYM(m_txtLen,        TXTRecordGetLength_f,      TXTRecordGetLength)
    LOAD_SYM(m_txtBytes,      TXTRecordGetBytesPtr_f,    TXTRecordGetBytesPtr)
    LOAD_SYM(m_txtDealloc,    TXTRecordDeallocate_f,     TXTRecordDeallocate)
#undef LOAD_SYM

    Logger::Get().Info("bonjour", "dnssd.dll loaded successfully");
    return true;
}

bool BonjourPublisher::EnsureLoaded() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return LoadDll();
}

bool BonjourPublisher::IsAvailable() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hDll != nullptr;
}

// ---------------------------------------------------------------------------
// Register
// ---------------------------------------------------------------------------
std::unique_ptr<Advertisement> BonjourPublisher::Register(
    const std::string&            name,
    const std::string&            type,
    uint16_t                      port,
    const std::vector<TxtRecord>& txtRecords)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hDll) return nullptr;

    // Build TXT record
    TXTRecordRef txtr{};
    m_txtCreate(&txtr, 0, nullptr);

    bool txtOk = true;
    for (const auto& rec : txtRecords) {
        const DNSServiceErrorType err = m_txtSet(
            &txtr,
            rec.key.c_str(),
            static_cast<uint8_t>(
                rec.value.size() > 255 ? 255 : rec.value.size()),
            rec.value.data());
        if (err != kDNSServiceErr_NoError) {
            Logger::Get().Warn("bonjour",
                "TXTRecordSetValue failed for key: " + rec.key);
            txtOk = false;
        }
    }

    const uint16_t txtLen   = m_txtLen(&txtr);
    const void*    txtBytes = m_txtBytes(&txtr);

    // port in network byte order
    const uint16_t netPort = htons(port);

    DNSServiceRef sdRef = nullptr;
    const DNSServiceErrorType err = m_register(
        &sdRef,
        0,                          // flags
        kDNSServiceInterfaceIndexAny,
        name.c_str(),
        type.c_str(),
        nullptr,                    // domain — default
        nullptr,                    // host   — this machine
        netPort,
        txtOk ? txtLen  : 0,
        txtOk ? txtBytes: nullptr,
        nullptr,                    // callback — fire-and-forget
        nullptr);

    m_txtDealloc(&txtr);

    if (err != kDNSServiceErr_NoError || !sdRef) {
        Logger::Get().Error("bonjour",
            "DNSServiceRegister failed",
            {{"type", type}, {"port", std::to_string(port)},
             {"err", std::to_string(err)}});
        return nullptr;
    }

    Logger::Get().Info("bonjour", "Registered",
        {{"name", name}, {"type", type}, {"port", std::to_string(port)}});

    auto ad        = std::make_unique<Advertisement>();
    ad->m_sdRef    = sdRef;
    ad->m_deallocFn = m_deallocate;
    return ad;
}

} // namespace bsa
