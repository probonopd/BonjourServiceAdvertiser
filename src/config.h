#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bsa {

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------
struct TxtRecord {
    std::string key;
    std::string value;
};

struct CustomAdvertisement {
    std::string            id;
    std::string            name;
    std::string            type;
    uint16_t               port       = 0;
    bool                   enabled    = true;
    std::vector<TxtRecord> txtRecords;
};

struct ServiceConfig {
    bool ssh      = true;
    bool smb      = true;
    bool rdp      = true;
    bool http     = true;
    bool https    = true;
    bool ftp      = true;
    bool webdav   = true;
    bool printers = true;

    std::vector<CustomAdvertisement> customAds;
};

// ---------------------------------------------------------------------------
// Config singleton
// ---------------------------------------------------------------------------
class Config {
public:
    static Config& Instance();

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    // Load (or reload) configuration from an INI file.
    // If the file does not exist a default is written first.
    void Load(const std::wstring& path);

    // Returns the currently loaded configuration.
    const ServiceConfig& Get() const;

    // Returns the path that was passed to Load().
    std::wstring GetPath() const;

    // Returns true when the advertisement is structurally valid.
    static bool Validate(const CustomAdvertisement& ad);

private:
    Config() = default;

    void WriteDefault(const std::wstring& path) const;

    mutable std::mutex m_mutex;
    ServiceConfig      m_config;
    std::wstring       m_path;
};

} // namespace bsa
