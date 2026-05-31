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
    return v;
}

} // namespace bsa
