#include "ReshadeSignature.h"
#include <array>
#include <cstdint>
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace core {
namespace {

constexpr DWORD kSha1Len = 20;

constexpr std::array<std::uint8_t, kSha1Len> kPinnedThumbprint = {
    0x58, 0x96, 0x90, 0x20, 0x8a, 0x5e, 0x52, 0xfb, 0x96, 0x98,
    0x0c, 0x4a, 0x66, 0x98, 0xf5, 0x0a, 0xcd, 0x47, 0xc4, 0x9f
};

bool thumbprintMatchesPin(const BYTE* hash, DWORD len) {
    if (len != kSha1Len)
        return false;
    BYTE diff = 0;
    for (DWORD i = 0; i < kSha1Len; ++i)
        diff |= static_cast<BYTE>(hash[i] ^ kPinnedThumbprint[i]);
    return diff == 0;
}

bool winVerifyTrustValid(const std::wstring& path) {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA wtd{};
    wtd.cbStruct = sizeof(wtd);
    wtd.dwUIChoice = WTD_UI_NONE;
    wtd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wtd.dwUnionChoice = WTD_CHOICE_FILE;
    wtd.pFile = &fileInfo;
    wtd.dwStateAction = WTD_STATEACTION_VERIFY;
    wtd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;

    LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wtd);

    wtd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wtd);

    // ReShade signs with a self-rooted certificate, so the chain never terminates in a
    // CA-trusted root. The pinned leaf thumbprint is our trust anchor, so an untrusted root
    // is acceptable; only a missing or tampered signature (any other failure) is rejected.
    return status == ERROR_SUCCESS || status == CERT_E_UNTRUSTEDROOT;
}

bool leafThumbprintMatchesPin(const std::wstring& path) {
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    PCMSG_SIGNER_INFO signerInfo = nullptr;
    PCCERT_CONTEXT certCtx = nullptr;
    bool match = false;

    BOOL queried = CryptQueryObject(
        CERT_QUERY_OBJECT_FILE,
        path.c_str(),
        CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
        CERT_QUERY_FORMAT_FLAG_BINARY,
        0, nullptr, nullptr, nullptr,
        &store, &msg, nullptr);

    if (!queried || store == nullptr || msg == nullptr) {
        if (msg) CryptMsgClose(msg);
        if (store) CertCloseStore(store, 0);
        return false;
    }

    DWORD signerSize = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerSize) && signerSize > 0) {
        signerInfo = static_cast<PCMSG_SIGNER_INFO>(LocalAlloc(LPTR, signerSize));
        if (signerInfo != nullptr &&
            CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signerInfo, &signerSize)) {
            CERT_INFO certInfo{};
            certInfo.Issuer = signerInfo->Issuer;
            certInfo.SerialNumber = signerInfo->SerialNumber;

            certCtx = CertFindCertificateInStore(
                store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);

            if (certCtx != nullptr) {
                BYTE hash[kSha1Len] = {};
                DWORD hashLen = sizeof(hash);
                if (CertGetCertificateContextProperty(certCtx, CERT_SHA1_HASH_PROP_ID, hash, &hashLen))
                    match = thumbprintMatchesPin(hash, hashLen);
            }
        }
    }

    if (certCtx) CertFreeCertificateContext(certCtx);
    if (signerInfo) LocalFree(signerInfo);
    if (msg) CryptMsgClose(msg);
    if (store) CertCloseStore(store, 0);
    return match;
}

}  // namespace

SignatureCheck verifyReshadeSignature(const std::filesystem::path& exe) {
    SignatureCheck result;

    std::error_code ec;
    if (!std::filesystem::exists(exe, ec) || ec) {
        result.status = SignatureStatus::NotSigned;
        result.reason = "Installer file not found.";
        return result;
    }

    const std::wstring path = exe.c_str();

    if (!winVerifyTrustValid(path)) {
        result.status = SignatureStatus::BadSignature;
        result.reason = "Installer is unsigned or its signature is invalid.";
        return result;
    }

    if (!leafThumbprintMatchesPin(path)) {
        result.status = SignatureStatus::WrongSigner;
        result.reason = "Installer is signed, but not by ReShade's certificate.";
        return result;
    }

    result.ok = true;
    result.status = SignatureStatus::Ok;
    result.reason = "Verified: signed by ReShade.";
    return result;
}

}
