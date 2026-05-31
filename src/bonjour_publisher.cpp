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
    if (m_sdRef) {
        if (m_removeFn) {
            for (auto r : m_recordRefs)
                if (r) m_removeFn(m_sdRef, r, 0);
            m_recordRefs.clear();
        }
        if (m_deallocFn)
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
        m_createConn    = nullptr;
        m_regRecord     = nullptr;
        m_removeRecord  = nullptr;
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

// Optional symbols — absent in very old Bonjour runtimes, but warn & skip
#define LOAD_SYM_OPT(member, type, symbol)                           \
    {                                                                 \
        void* _p = reinterpret_cast<void*>(                          \
                       GetProcAddress(m_hDll, #symbol));             \
        if (_p) memcpy(&(member), &_p, sizeof(_p));                  \
        else Logger::Get().Warn("bonjour",                           \
            "Optional symbol not found: " #symbol);                  \
    }
    LOAD_SYM_OPT(m_createConn,  DNSServiceCreateConnection_f, DNSServiceCreateConnection)
    LOAD_SYM_OPT(m_regRecord,   DNSServiceRegisterRecord_f,   DNSServiceRegisterRecord)
    LOAD_SYM_OPT(m_removeRecord,DNSServiceRemoveRecord_f,     DNSServiceRemoveRecord)
#undef LOAD_SYM_OPT
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

// ---------------------------------------------------------------------------
// Wire-format helpers
// ---------------------------------------------------------------------------
namespace {

// Encode a DNS name to wire format labels.
// e.g., "MUSIC._device-info._tcp.local." → \x05MUSIC\x0c_device-info...
static std::vector<uint8_t> WireName(const std::string& fqdn)
{
    std::vector<uint8_t> out;
    std::string rest = fqdn;
    if (!rest.empty() && rest.back() == '.') rest.pop_back();
    for (size_t pos = 0; pos <= rest.size(); ) {
        size_t dot = rest.find('.', pos);
        size_t end = (dot == std::string::npos) ? rest.size() : dot;
        std::string lbl = rest.substr(pos, end - pos);
        if (lbl.empty()) break;
        out.push_back(static_cast<uint8_t>(lbl.size()));
        for (char c : lbl) out.push_back(static_cast<uint8_t>(c));
        pos = (dot == std::string::npos) ? rest.size() + 1 : dot + 1;
    }
    out.push_back(0);
    return out;
}

// Build TXT rdata: concatenated length-prefixed "key=value" strings.
static std::vector<uint8_t> WireTxt(const std::vector<TxtRecord>& recs)
{
    std::vector<uint8_t> out;
    for (const auto& r : recs) {
        std::string e = r.key + "=" + r.value;
        if (e.size() > 255) e.resize(255);
        out.push_back(static_cast<uint8_t>(e.size()));
        for (char c : e) out.push_back(static_cast<uint8_t>(c));
    }
    if (out.empty()) out.push_back(0);
    return out;
}

// Build SRV rdata: priority(2) + weight(2) + port(2) + target_name.
static std::vector<uint8_t> WireSrv(uint16_t port, const std::string& target)
{
    std::vector<uint8_t> out;
    out.push_back(0); out.push_back(0); // priority = 0
    out.push_back(0); out.push_back(0); // weight   = 0
    out.push_back(static_cast<uint8_t>(port >> 8));
    out.push_back(static_cast<uint8_t>(port & 0xFF));
    auto t = WireName(target);
    out.insert(out.end(), t.begin(), t.end());
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RegisterDeviceInfo
// Publishes _device-info._tcp by directly registering the underlying DNS
// records (PTR + TXT + SRV), bypassing the mDNSResponder filtering that
// suppresses DNSServiceRegister() calls for this reserved service type.
// ---------------------------------------------------------------------------
std::unique_ptr<Advertisement> BonjourPublisher::RegisterDeviceInfo(
    const std::string&            instanceName,
    const std::string&            hostName,
    const std::vector<TxtRecord>& txt)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hDll || !m_createConn || !m_regRecord) {
        Logger::Get().Warn("bonjour",
            "Raw record API unavailable; device-info not published");
        return nullptr;
    }

    // Open a shared DNS-SD connection
    DNSServiceRef connRef = nullptr;
    if (m_createConn(&connRef) != kDNSServiceErr_NoError || !connRef)
        return nullptr;

    auto adv       = std::make_unique<Advertisement>();
    adv->m_sdRef    = connRef;
    adv->m_deallocFn = m_deallocate;
    adv->m_removeFn  = m_removeRecord;

    const std::string instFull  = instanceName + "._device-info._tcp.local.";
    const std::string srvTarget = hostName      + ".local.";

    // -- PTR record: _device-info._tcp.local. → INSTANCENAME._device-info._tcp.local.
    {
        auto rdata = WireName(instFull);
        DNSRecordRef ref = nullptr;
        DNSServiceErrorType e = m_regRecord(connRef, &ref,
                kDNSServiceFlagsShared, kDNSServiceInterfaceIndexAny,
                "_device-info._tcp.local.",
                kDNSServiceType_PTR, kDNSServiceClass_IN,
                static_cast<uint16_t>(rdata.size()), rdata.data(),
                0, nullptr, nullptr);
        if (e == kDNSServiceErr_NoError && ref)
            adv->m_recordRefs.push_back(ref);
        else
            Logger::Get().Warn("bonjour", "device-info PTR failed",
                {{"err", std::to_string(e)}});
    }

    // -- TXT record: INSTANCENAME._device-info._tcp.local.
    {
        auto rdata = WireTxt(txt);
        DNSRecordRef ref = nullptr;
        DNSServiceErrorType e = m_regRecord(connRef, &ref,
                kDNSServiceFlagsShared, kDNSServiceInterfaceIndexAny,
                instFull.c_str(),
                kDNSServiceType_TXT, kDNSServiceClass_IN,
                static_cast<uint16_t>(rdata.size()), rdata.data(),
                0, nullptr, nullptr);
        if (e == kDNSServiceErr_NoError && ref)
            adv->m_recordRefs.push_back(ref);
        else
            Logger::Get().Warn("bonjour", "device-info TXT failed",
                {{"err", std::to_string(e)}});
    }

    // -- SRV record: INSTANCENAME._device-info._tcp.local. → hostname.local. port 0
    {
        auto rdata = WireSrv(0, srvTarget);
        DNSRecordRef ref = nullptr;
        DNSServiceErrorType e = m_regRecord(connRef, &ref,
                kDNSServiceFlagsShared, kDNSServiceInterfaceIndexAny,
                instFull.c_str(),
                kDNSServiceType_SRV, kDNSServiceClass_IN,
                static_cast<uint16_t>(rdata.size()), rdata.data(),
                0, nullptr, nullptr);
        if (e == kDNSServiceErr_NoError && ref)
            adv->m_recordRefs.push_back(ref);
        else
            Logger::Get().Warn("bonjour", "device-info SRV failed",
                {{"err", std::to_string(e)}});
    }

    if (adv->m_recordRefs.empty()) {
        Logger::Get().Warn("bonjour", "device-info: all record registrations failed");
        return nullptr;
    }

    Logger::Get().Info("bonjour", "Registered device-info",
        {{"name", instanceName}, {"host", hostName}});
    return adv;
}

} // namespace bsa
