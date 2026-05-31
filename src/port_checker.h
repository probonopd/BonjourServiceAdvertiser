#pragma once

#include <cstdint>

namespace bsa {

/// Returns true if any process is listening on the given TCP port.
bool IsPortListening(uint16_t port);

/// Returns true if the named Windows service is in the SERVICE_RUNNING state.
bool IsWindowsServiceRunning(const wchar_t* serviceName);

/// Returns true if there is at least one non-loopback, non-tunnel network
/// interface that is currently UP.
bool HasNonLoopbackInterface();

} // namespace bsa
