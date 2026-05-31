#pragma once

#include "detectors/detector.h"
#include "bonjour_publisher.h"
#include "config.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bsa {

/// Factory declared in all_detectors.cpp
std::vector<std::unique_ptr<IDetector>> CreateBuiltinDetectors();

// ---------------------------------------------------------------------------
// DetectorManager
//
// Owns all detectors and the registry of live advertisements.
// Call Reconcile() whenever something might have changed.
// ---------------------------------------------------------------------------
class DetectorManager {
public:
    DetectorManager();
    ~DetectorManager() = default;

    DetectorManager(const DetectorManager&)            = delete;
    DetectorManager& operator=(const DetectorManager&) = delete;

    /// Perform a full reconciliation pass:
    ///   - Remove advertisements that are no longer active
    ///   - Add advertisements for newly-active services
    /// Thread-safe.
    void Reconcile(BonjourPublisher& publisher, const ServiceConfig& cfg);

    /// Remove all active advertisements (called on shutdown).
    void Clear();

private:
    // key = "<type>:<port>" e.g. "_ssh._tcp:22"
    static std::string MakeKey(const std::string& type, uint16_t port);

    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<IDetector>>              m_detectors;
    std::unordered_map<std::string,
                       std::unique_ptr<Advertisement>>   m_active;
};

} // namespace bsa
