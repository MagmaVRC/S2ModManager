#include "Upnp.h"

// We link miniupnpc as a static lib; without this its headers decorate every
// function with __declspec(dllimport), producing __imp_ link errors.
#define MINIUPNP_STATICLIB
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#include <array>
#include <string>

namespace core::net {
namespace {

/// <summary>A discovered, connected IGD plus the LAN address of this host.</summary>
struct Igd {
    bool         ok = false;
    UPNPUrls     urls{};
    IGDdatas     data{};
    std::string  lanAddr;
    std::string  error;
};

/// <summary>Discovers the local gateway. Caller must FreeUPNPUrls(&igd.urls) when ok.</summary>
Igd discover() {
    Igd igd;
    int err = 0;
    UPNPDev* devlist = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &err);
    if (!devlist) {
        igd.error = "No UPnP gateway found on the network.";
        return igd;
    }

    std::array<char, 64> lan{};
#if defined(MINIUPNPC_API_VERSION) && (MINIUPNPC_API_VERSION >= 18)
    std::array<char, 64> wan{};
    int valid = UPNP_GetValidIGD(devlist, &igd.urls, &igd.data,
                                 lan.data(), static_cast<int>(lan.size()),
                                 wan.data(), static_cast<int>(wan.size()));
#else
    int valid = UPNP_GetValidIGD(devlist, &igd.urls, &igd.data,
                                 lan.data(), static_cast<int>(lan.size()));
#endif
    freeUPNPDevlist(devlist);

    if (valid != 1) {   // 1 == connected IGD; anything else can't port-forward
        if (valid != 0)
            FreeUPNPUrls(&igd.urls);
        igd.error = "UPnP gateway found but not usable (no connected IGD).";
        return igd;
    }

    igd.lanAddr = lan.data();
    igd.ok = true;
    return igd;
}

}  // namespace

UpnpMapping map(std::uint16_t port, const std::string& description) {
    UpnpMapping result;

    Igd igd = discover();
    if (!igd.ok) {
        result.error = igd.error;
        return result;
    }

    const std::string portStr = std::to_string(port);

    // remoteHost must be "" (wildcard); nullptr trips 402 Invalid Args on many IGDs.
    int rc = UPNP_AddPortMapping(igd.urls.controlURL, igd.data.first.servicetype,
                                 portStr.c_str(),     // external port
                                 portStr.c_str(),     // internal port
                                 igd.lanAddr.c_str(), // internal client
                                 description.c_str(),
                                 "TCP",
                                 "",                   // remote host: wildcard
                                 "43200");             // 12-hour lease (auto-expires if we crash)
    // 725 OnlyPermanentLeasesSupported: some NATs reject timed leases — retry permanent.
    if (rc == 725)
        rc = UPNP_AddPortMapping(igd.urls.controlURL, igd.data.first.servicetype,
                                 portStr.c_str(), portStr.c_str(), igd.lanAddr.c_str(),
                                 description.c_str(), "TCP", "", "0");
    if (rc != UPNPCOMMAND_SUCCESS) {
        result.error = std::string("Router refused the port mapping: ") + strupnperror(rc);
        FreeUPNPUrls(&igd.urls);
        return result;
    }

    std::array<char, 64> extIp{};
    int ipRc = UPNP_GetExternalIPAddress(igd.urls.controlURL, igd.data.first.servicetype, extIp.data());
    FreeUPNPUrls(&igd.urls);

    if (ipRc != UPNPCOMMAND_SUCCESS || extIp[0] == '\0') {
        unmap(port);
        result.error = "Mapped the port but could not read the router's external IP.";
        return result;
    }

    result.ok = true;
    result.externalIp = extIp.data();
    result.port = port;
    return result;
}

void unmap(std::uint16_t port) {
    Igd igd = discover();
    if (!igd.ok)
        return;
    const std::string portStr = std::to_string(port);
    (void)UPNP_DeletePortMapping(igd.urls.controlURL, igd.data.first.servicetype,
                                 portStr.c_str(), "TCP", "");
    FreeUPNPUrls(&igd.urls);
}

}  // namespace core::net
