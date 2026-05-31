#include "port_checker.h"

// Winsock2 MUST be included before windows.h
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winsvc.h>

#include <cstring>
#include <memory>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace bsa {

// ---------------------------------------------------------------------------
// IsPortListening
// Checks IPv4 and IPv6 TCP listening table for the given port.
// ---------------------------------------------------------------------------
bool IsPortListening(uint16_t port) {
    // ----- IPv4 -----
    {
        ULONG bufSize = 0;
        GetExtendedTcpTable(nullptr, &bufSize, FALSE,
                            AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);

        std::vector<BYTE> buf(bufSize + 1024);
        if (GetExtendedTcpTable(buf.data(), &bufSize, FALSE,
                                AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0)
            == NO_ERROR)
        {
            const auto* table =
                reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                // LocalPort is in network byte order stored as DWORD
                const uint16_t p =
                    ntohs(static_cast<uint16_t>(
                        table->table[i].dwLocalPort & 0xFFFF));
                if (p == port) return true;
            }
        }
    }

    // ----- IPv6 -----
    {
        ULONG bufSize = 0;
        GetExtendedTcpTable(nullptr, &bufSize, FALSE,
                            AF_INET6, TCP_TABLE_OWNER_PID_LISTENER, 0);

        std::vector<BYTE> buf(bufSize + 1024);
        if (GetExtendedTcpTable(buf.data(), &bufSize, FALSE,
                                AF_INET6, TCP_TABLE_OWNER_PID_LISTENER, 0)
            == NO_ERROR)
        {
            const auto* table =
                reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const uint16_t p =
                    ntohs(static_cast<uint16_t>(
                        table->table[i].dwLocalPort & 0xFFFF));
                if (p == port) return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// IsWindowsServiceRunning
// ---------------------------------------------------------------------------
bool IsWindowsServiceRunning(const wchar_t* serviceName) {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return false;

    SC_HANDLE hSvc = OpenServiceW(hScm, serviceName, SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hScm);
        return false;
    }

    SERVICE_STATUS ss{};
    const bool running =
        QueryServiceStatus(hSvc, &ss) &&
        ss.dwCurrentState == SERVICE_RUNNING;

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    return running;
}

// ---------------------------------------------------------------------------
// HasNonLoopbackInterface
// ---------------------------------------------------------------------------
bool HasNonLoopbackInterface() {
    ULONG bufLen = 15000; // initial guess per MSDN recommendation
    std::vector<BYTE> buf(bufLen);

    for (int tries = 0; tries < 3; ++tries) {
        ULONG ret = GetAdaptersAddresses(
            AF_UNSPEC,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()),
            &bufLen);

        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.resize(bufLen);
            continue;
        }
        if (ret != NO_ERROR) return false;

        const auto* adapter =
            reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buf.data());
        while (adapter) {
            // Must be UP, not loopback, not tunnel
            if (adapter->OperStatus == IfOperStatusUp &&
                adapter->IfType    != IF_TYPE_SOFTWARE_LOOPBACK &&
                adapter->IfType    != IF_TYPE_TUNNEL)
            {
                return true;
            }
            adapter = adapter->Next;
        }
        return false; // found adapters but none qualify
    }
    return false;
}

} // namespace bsa
