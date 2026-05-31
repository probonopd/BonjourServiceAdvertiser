#include "config.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>
#include <string>

namespace bsa {

// ---------------------------------------------------------------------------
// Internal helpers (file-local)
// ---------------------------------------------------------------------------
namespace {

static std::string Trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto  b  = s.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

static std::string ToLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool ParseBool(const std::string& s, bool defaultVal = false) {
    const std::string l = ToLower(Trim(s));
    if (l == "true"  || l == "1" || l == "yes" || l == "on")  return true;
    if (l == "false" || l == "0" || l == "no"  || l == "off") return false;
    return defaultVal;
}

// Read an entire file into a std::string via Win32.
// Returns empty string on any error.
static std::string ReadFileToString(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart > 10LL * 1024 * 1024) {
        CloseHandle(hFile);
        return {};
    }

    const auto   byteCount = static_cast<DWORD>(sz.QuadPart);
    std::string  content(byteCount, '\0');
    DWORD        bytesRead = 0;

    if (!ReadFile(hFile, content.data(), byteCount, &bytesRead, nullptr)) {
        CloseHandle(hFile);
        return {};
    }

    content.resize(bytesRead);
    CloseHandle(hFile);
    return content;
}

// Write a NUL-terminated C-string to a file via Win32.
static bool WriteStringToFile(const std::wstring& path, const char* text) {
    HANDLE hFile = CreateFileW(path.c_str(),
                               GENERIC_WRITE,
                               0,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    const DWORD len = static_cast<DWORD>(std::char_traits<char>::length(text));
    DWORD written   = 0;
    const bool ok   = WriteFile(hFile, text, len, &written, nullptr) != FALSE;
    CloseHandle(hFile);
    return ok && written == len;
}

// Default INI content written when no config file exists.
static const char kDefaultConfig[] =
    "; BonjourServiceAdvertiser configuration\n"
    "; Generated automatically - edit as needed\n"
    "\n"
    "[services]\n"
    "ssh=true\n"
    "smb=true\n"
    "rdp=true\n"
    "http=true\n"
    "https=true\n"
    "ftp=true\n"
    "webdav=true\n"
    "printers=true\n"
    "\n"
    "; -----------------------------------------------------------------\n"
    "; Custom advertisement example (remove the leading ';' to activate)\n"
    "; -----------------------------------------------------------------\n"
    "; [custom:MyWebApp]\n"
    "; name=My Web Application\n"
    "; type=_http._tcp\n"
    "; port=8080\n"
    "; enabled=true\n"
    "; txt.path=/app\n"
    "; txt.version=1.0\n"
    ";\n"
    "; [custom:MyPrinter]\n"
    "; name=Office Printer\n"
    "; type=_ipp._tcp\n"
    "; port=631\n"
    "; enabled=true\n"
    "; txt.rp=ipp/print\n";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
Config& Config::Instance() {
    static Config instance;
    return instance;
}

const ServiceConfig& Config::Get() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

std::wstring Config::GetPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_path;
}

// ---------------------------------------------------------------------------
// WriteDefault
// ---------------------------------------------------------------------------
void Config::WriteDefault(const std::wstring& path) const {
    WriteStringToFile(path, kDefaultConfig);
}

// ---------------------------------------------------------------------------
// Validate
// ---------------------------------------------------------------------------
bool Config::Validate(const CustomAdvertisement& ad) {
    if (ad.name.empty()) return false;
    if (ad.port < 1)     return false;  // port > 65535 impossible with uint16_t

    // Service type must match _<label>._tcp or _<label>._udp
    static const std::regex kTypeRx(
        R"(^_[a-zA-Z0-9\-]+\._(tcp|udp)$)",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(ad.type, kTypeRx)) return false;

    // Each TXT key and value must be ≤ 255 bytes
    for (const auto& rec : ad.txtRecords) {
        if (rec.key.size()   > 255) return false;
        if (rec.value.size() > 255) return false;
        if (rec.key.empty())        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
void Config::Load(const std::wstring& path) {
    // Write default config if the file does not yet exist
    {
        const DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            WriteDefault(path);
        }
    }

    const std::string content = ReadFileToString(path);

    ServiceConfig cfg;  // starts with all built-ins enabled (default members)

    // -----------------------------------------------------------------------
    // Parse INI
    // -----------------------------------------------------------------------
    enum class Section { None, Services, Custom } section = Section::None;

    // Pointer to the CustomAdvertisement currently being populated (if any)
    CustomAdvertisement* curCustom = nullptr;

    std::istringstream ss(content);
    std::string        rawLine;

    while (std::getline(ss, rawLine)) {
        const std::string line = Trim(rawLine);

        // Skip blank lines and comments
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // ----------------------------------------------------------------
        // Section header
        // ----------------------------------------------------------------
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close == std::string::npos) continue;

            const std::string hdr = Trim(line.substr(1, close - 1));

            if (ToLower(hdr) == "services") {
                section   = Section::Services;
                curCustom = nullptr;
            } else if (hdr.size() > 7 &&
                       ToLower(hdr.substr(0, 7)) == "custom:") {
                const std::string id = Trim(hdr.substr(7));
                if (id.empty()) {
                    section   = Section::None;
                    curCustom = nullptr;
                } else {
                    section = Section::Custom;
                    cfg.customAds.push_back(CustomAdvertisement{});
                    curCustom     = &cfg.customAds.back();
                    curCustom->id = id;
                }
            } else {
                section   = Section::None;
                curCustom = nullptr;
            }
            continue;
        }

        // ----------------------------------------------------------------
        // Key = Value
        // ----------------------------------------------------------------
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));

        if (section == Section::Services) {
            const std::string lkey = ToLower(key);
            if      (lkey == "ssh")      cfg.ssh      = ParseBool(val, true);
            else if (lkey == "smb")      cfg.smb      = ParseBool(val, true);
            else if (lkey == "rdp")      cfg.rdp      = ParseBool(val, true);
            else if (lkey == "http")     cfg.http     = ParseBool(val, true);
            else if (lkey == "https")    cfg.https    = ParseBool(val, true);
            else if (lkey == "ftp")      cfg.ftp      = ParseBool(val, true);
            else if (lkey == "webdav")   cfg.webdav   = ParseBool(val, true);
            else if (lkey == "printers") cfg.printers = ParseBool(val, true);
        } else if (section == Section::Custom && curCustom != nullptr) {
            const std::string lkey = ToLower(key);

            if (lkey == "name") {
                curCustom->name = val;
            } else if (lkey == "type") {
                curCustom->type = val;
            } else if (lkey == "port") {
                const int p = std::atoi(val.c_str());
                curCustom->port = (p >= 1 && p <= 65535)
                                  ? static_cast<uint16_t>(p)
                                  : 0;
            } else if (lkey == "enabled") {
                curCustom->enabled = ParseBool(val, true);
            } else if (key.size() > 4 &&
                       ToLower(key.substr(0, 4)) == "txt.") {
                const std::string txtKey = Trim(key.substr(4));
                if (!txtKey.empty()) {
                    curCustom->txtRecords.push_back(TxtRecord{txtKey, val});
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Remove invalid custom advertisements
    // -----------------------------------------------------------------------
    cfg.customAds.erase(
        std::remove_if(cfg.customAds.begin(), cfg.customAds.end(),
                       [](const CustomAdvertisement& a) {
                           return !Config::Validate(a);
                       }),
        cfg.customAds.end());

    // -----------------------------------------------------------------------
    // Commit
    // -----------------------------------------------------------------------
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = std::move(cfg);
    m_path   = path;
}

} // namespace bsa
