#pragma once
#include <filesystem>
#include <string>

namespace core {

/// <summary>Outcome categories for ReShade installer signature verification.</summary>
enum class SignatureStatus {
    Ok,            // Trust verified and the leaf thumbprint matches the pin.
    NotSigned,     // File has no embedded Authenticode signature, or is missing.
    BadSignature,  // Signature present but WinVerifyTrust rejected it.
    WrongSigner    // Trust valid, but the signer leaf SHA1 does not match the pin.
};

/// <summary>Result of verifying a downloaded executable's Authenticode signature.</summary>
struct SignatureCheck {
    bool ok = false;
    SignatureStatus status = SignatureStatus::NotSigned;
    std::string reason;   // ASCII message suitable for a toast
};

/// <summary>Verifies that an executable carries a valid Authenticode signature and that its
/// signer leaf certificate SHA1 thumbprint equals ReShade's pinned value. Offline-friendly:
/// no CRL/OCSP network retrieval is performed.</summary>
/// <param name="exe">Path to the .exe to verify (the ReShade setup binary).</param>
/// <returns>A <see cref="SignatureCheck"/>; ok is true only when both checks pass.</returns>
[[nodiscard]] SignatureCheck verifyReshadeSignature(const std::filesystem::path& exe);

}
