// detectors/all_detectors.cpp
// Compile-unit that defines all eight built-in detector classes.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winspool.h>

#include <memory>
#include <vector>

#include "detector.h"
#include "../port_checker.h"

namespace bsa {

// ---------------------------------------------------------------------------
// Macro to generate a simple detector whose only check is:
//   service SVCNAME is running  AND  port PORT is listening
// ---------------------------------------------------------------------------
#define SIMPLE_DETECTOR(ClassName, DisplayName, SvcType, SvcPort, WinSvc)   \
class ClassName final : public IDetector {                                   \
public:                                                                       \
    const char* Name()        const override { return DisplayName; }         \
    const char* ServiceType() const override { return SvcType; }             \
    uint16_t    Port()        const override { return SvcPort; }             \
    bool IsActive() const override {                                          \
        return IsWindowsServiceRunning(WinSvc)                               \
            && IsPortListening(SvcPort)                                      \
            && HasNonLoopbackInterface();                                     \
    }                                                                         \
};

// ---------------------------------------------------------------------------
// SSH — requires OpenSSH service (sshd) and port 22
// ---------------------------------------------------------------------------
SIMPLE_DETECTOR(SshDetector,    "SSH",       "_ssh._tcp",      22,  L"sshd")

// ---------------------------------------------------------------------------
// SFTP — co-advertise SSH also as sftp-ssh (same port/service, RFC 4253)
// ---------------------------------------------------------------------------
SIMPLE_DETECTOR(SftpDetector,   "SFTP",      "_sftp-ssh._tcp", 22,  L"sshd")

// ---------------------------------------------------------------------------
// SMB — requires LanmanServer and port 445
// ---------------------------------------------------------------------------
SIMPLE_DETECTOR(SmbDetector,    "SMB",    "_smb._tcp",  445,  L"LanmanServer")

// ---------------------------------------------------------------------------
// RDP — requires TermService and port 3389
// ---------------------------------------------------------------------------
SIMPLE_DETECTOR(RdpDetector,    "RDP",    "_rdp._tcp",  3389, L"TermService")

// ---------------------------------------------------------------------------
// FTP — requires FTPSVC (IIS FTP) and port 21
// ---------------------------------------------------------------------------
SIMPLE_DETECTOR(FtpDetector,    "FTP",    "_ftp._tcp",  21,   L"FTPSVC")

// ---------------------------------------------------------------------------
// HTTP — port 80 listening (any process); no required Windows service
// ---------------------------------------------------------------------------
class HttpDetector final : public IDetector {
public:
    const char* Name()        const override { return "HTTP"; }
    const char* ServiceType() const override { return "_http._tcp"; }
    uint16_t    Port()        const override { return 80; }
    bool IsActive() const override {
        return IsPortListening(80) && HasNonLoopbackInterface();
    }
};

// ---------------------------------------------------------------------------
// HTTPS — port 443 listening (any process)
// ---------------------------------------------------------------------------
class HttpsDetector final : public IDetector {
public:
    const char* Name()        const override { return "HTTPS"; }
    const char* ServiceType() const override { return "_https._tcp"; }
    uint16_t    Port()        const override { return 443; }
    bool IsActive() const override {
        return IsPortListening(443) && HasNonLoopbackInterface();
    }
};

// ---------------------------------------------------------------------------
// WebDAV — requires W3SVC (IIS) and port 80 or 443 (check both)
// ---------------------------------------------------------------------------
class WebDavDetector final : public IDetector {
public:
    const char* Name()        const override { return "WebDAV"; }
    const char* ServiceType() const override { return "_webdav._tcp"; }
    uint16_t    Port()        const override { return 80; }
    bool IsActive() const override {
        return IsWindowsServiceRunning(L"W3SVC")
            && (IsPortListening(80) || IsPortListening(443))
            && HasNonLoopbackInterface();
    }
    std::vector<TxtRecord> TxtRecords() const override {
        return {{"path", "/"}};
    }
};

// ---------------------------------------------------------------------------
// Printers — at least one shared printer available; advertise IPP on 631
// ---------------------------------------------------------------------------
class PrinterDetector final : public IDetector {
public:
    const char* Name()        const override { return "Printers"; }
    const char* ServiceType() const override { return "_ipp._tcp"; }
    uint16_t    Port()        const override { return 631; }
    bool IsActive() const override {
        if (!HasNonLoopbackInterface()) return false;
        if (!IsWindowsServiceRunning(L"Spooler")) return false;

        // Enumerate shared printers
        DWORD needed = 0, count = 0;
        EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_SHARED,
                      nullptr, 4, nullptr, 0, &needed, &count);
        if (needed == 0) return false;

        std::vector<BYTE> buf(needed);
        if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_SHARED,
                           nullptr, 4, buf.data(), needed, &needed, &count))
        {
            return false;
        }
        return count > 0;
    }
    std::vector<TxtRecord> TxtRecords() const override {
        return {{"rp", "ipp/print"}};
    }
};

// ---------------------------------------------------------------------------
// Helper: read a REG_SZ / REG_EXPAND_SZ value from the Windows registry.
// Returns fallback when the key/value does not exist.
// ---------------------------------------------------------------------------
static std::string ReadRegString(HKEY root, const wchar_t* path,
                                  const wchar_t* valueName,
                                  const char*    fallback)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return fallback;
    wchar_t buf[512] = {};
    DWORD   bytes    = sizeof(buf);
    DWORD   type     = 0;
    LSTATUS rc       = RegQueryValueExW(hk, valueName, nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &bytes);
    RegCloseKey(hk);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return fallback;
    // Convert wide → narrow (safe for ASCII product/build strings)
    std::string out;
    out.reserve(wcslen(buf));
    for (const wchar_t* p = buf; *p; ++p)
        out += static_cast<char>(*p);
    return out.empty() ? fallback : out;
}

// ---------------------------------------------------------------------------
// Device Info — advertise _device-info._tcp port 0 with hardware model and
// OS version, mirroring the record that macOS publishes so that Bonjour
// browsers (e.g. Discovery, dns-sd, avahi-browse) can show the correct
// device icon and OS information.
// ---------------------------------------------------------------------------
class DeviceInfoDetector final : public IDetector {
    std::string m_model;
    std::string m_osVers;
public:
    DeviceInfoDetector() {
        // Hardware model, e.g. "Virtual Machine" or "Latitude 7490"
        m_model = ReadRegString(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\SystemInformation",
            L"SystemProductName", "PC");

        // OS product name, e.g. "Windows 11 Enterprise"
        std::string product = ReadRegString(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"ProductName", "Windows");
        // Build number, e.g. "26100"
        std::string build = ReadRegString(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"CurrentBuild", "");
        m_osVers = product;
        if (!build.empty())
            m_osVers += " (Build " + build + ")";
    }

    const char* Name()        const override { return "DeviceInfo"; }
    const char* ServiceType() const override { return "_device-info._tcp"; }
    uint16_t    Port()        const override { return 0; }
    bool IsActive() const override { return HasNonLoopbackInterface(); }
    std::vector<TxtRecord> TxtRecords() const override {
        return {{"model", m_model}, {"osVers", m_osVers}};
    }
};

// ---------------------------------------------------------------------------
// Factory: creates all built-in detector instances
// ---------------------------------------------------------------------------
std::vector<std::unique_ptr<IDetector>> CreateBuiltinDetectors() {
    std::vector<std::unique_ptr<IDetector>> v;
    v.push_back(std::make_unique<SshDetector>());
    v.push_back(std::make_unique<SftpDetector>());
    v.push_back(std::make_unique<SmbDetector>());
    v.push_back(std::make_unique<RdpDetector>());
    v.push_back(std::make_unique<HttpDetector>());
    v.push_back(std::make_unique<HttpsDetector>());
    v.push_back(std::make_unique<FtpDetector>());
    v.push_back(std::make_unique<WebDavDetector>());
    v.push_back(std::make_unique<PrinterDetector>());
    v.push_back(std::make_unique<DeviceInfoDetector>());
    return v;
}

} // namespace bsa
