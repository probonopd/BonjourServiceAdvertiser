#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../config.h"

namespace bsa {

// ---------------------------------------------------------------------------
// IDetector — interface for built-in service detectors
// ---------------------------------------------------------------------------
struct IDetector {
    virtual ~IDetector() = default;

    /// Human-readable name used in log messages.
    virtual const char* Name() const = 0;

    /// DNS-SD service type, e.g. "_ssh._tcp".
    virtual const char* ServiceType() const = 0;

    /// Port this service listens on.
    virtual uint16_t Port() const = 0;

    /// Optional TXT records to attach (empty by default).
    virtual std::vector<TxtRecord> TxtRecords() const { return {}; }

    /// Returns true when the service is currently running and the port
    /// is listening, AND at least one non-loopback interface is up.
    virtual bool IsActive() const = 0;
};

} // namespace bsa
