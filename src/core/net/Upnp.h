#pragma once
#include <cstdint>
#include <string>

namespace core::net {

/// <summary>Result of a UPnP port-mapping attempt.</summary>
struct UpnpMapping {
    bool        ok = false;        // true when both discovery and AddPortMapping succeeded
    std::string externalIp;        // router's WAN IPv4 in dotted form (only when ok)
    std::uint16_t port = 0;        // the mapped external/internal TCP port (only when ok)
    std::string error;             // human-readable reason when !ok
};

/// <summary>Discovers the local IGD and adds a TCP port mapping to this machine.
/// The mapping survives until <see cref="unmap"/> is called or the process exits.</summary>
/// <remarks>Requires an IGD (router) with UPnP enabled. CGNAT environments may report
/// success while remaining unreachable from the public internet — that is out of scope.</remarks>
/// <param name="port">External and internal TCP port to map.</param>
/// <param name="description">Mapping description shown in router UIs.</param>
/// <returns>A populated <see cref="UpnpMapping"/>; check <c>ok</c>.</returns>
[[nodiscard]] UpnpMapping map(std::uint16_t port, const std::string& description = "S2ModManager");

/// <summary>Removes a previously added TCP mapping. Best-effort; ignores errors.</summary>
void unmap(std::uint16_t port);

}
