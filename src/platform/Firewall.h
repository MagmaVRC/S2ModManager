#pragma once
#include <string>

namespace platform {

/// <summary>Adds (or refreshes) an inbound "allow" Windows Firewall rule for this
/// executable so peers can reach the share listener. Best-effort: requires the process
/// to be elevated; returns false (a no-op) otherwise.</summary>
/// <param name="ruleName">Display name of the firewall rule; an existing rule with the
/// same name is replaced (clearing any stale block).</param>
/// <returns>True if the allow rule is in place.</returns>
bool ensureInboundAllowed(const std::wstring& ruleName);

}
