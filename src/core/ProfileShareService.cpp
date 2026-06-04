#include "ProfileShareService.h"

#include "Compression.h"
#include "ConnectionKey.h"
#include "Http.h"
#include "PeerSession.h"
#include "ProfileBundle.h"
#include "net/Upnp.h"

#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <random>

namespace core {
namespace {

// Random ephemeral port range for the UPnP mapping (IANA dynamic range); the chosen
// port travels in the connection key, so the peer dials whatever the host picked.
constexpr std::uint16_t kPortMin = 49152;
constexpr std::uint16_t kPortMax = 65535;
constexpr int kPortAttempts = 3;
// Greeting that gates non-app connectors: "S2MP" + protocol version byte.
constexpr std::array<std::uint8_t, 5> kGreeting = { 'S', '2', 'M', 'P', 1 };
constexpr std::uint32_t kMaxHandshakeFrame = 1024;
constexpr std::uint32_t kMaxManifestFrame  = 16u * 1024 * 1024;

void putU64(Bytes& b, std::uint64_t v) {
    for (int i = 7; i >= 0; --i)
        b.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}

std::uint64_t getU64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

// Loose dotted-IPv4 check — enough to reject an error page or empty body from the IP service.
bool looksLikeIpv4(const std::string& s) {
    int dots = 0, digits = 0, value = 0;
    for (char c : s) {
        if (c == '.') { if (digits == 0) return false; ++dots; digits = 0; value = 0; }
        else if (std::isdigit(static_cast<unsigned char>(c))) {
            if (++digits > 3) return false;
            value = value * 10 + (c - '0');
            if (value > 255) return false;
        }
        else return false;
    }
    return dots == 3 && digits > 0;
}

// Asks a public endpoint for our WAN IP (manual-port mode has no UPnP to report it).
std::optional<std::string> detectPublicIp() {
    auto body = httpGet("https://checkip.amazonaws.com/");
    if (!body)
        return std::nullopt;
    std::string ip = trim(*body);
    if (!looksLikeIpv4(ip))
        return std::nullopt;
    return ip;
}

}  // namespace

// A live session pointer the worker publishes so stop() can interrupt blocking I/O.
// Kept here (not in the header) to avoid leaking PeerSession into every includer.
namespace {
std::mutex             g_sessionMutex;
std::unique_ptr<PeerSession> g_session;
}

ProfileShareService::~ProfileShareService() {
    stop();
}

void ProfileShareService::setStatus(ShareState state, std::string message) {
    state_.store(state);
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(message);
}

void ProfileShareService::fail(std::string message) {
    setStatus(ShareState::Failed, std::move(message));
}

std::string ProfileShareService::connectionKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return key_;
}

std::string ProfileShareService::statusMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::optional<std::string> ProfileShareService::takeImportedProfile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!importedReady_)
        return std::nullopt;
    importedReady_ = false;
    return importedProfile_;
}

void ProfileShareService::startHosting(const GamePaths& paths, ProfileStore& store,
                                       const std::string& profileName, const HostOptions& opts) {
    if (running_.exchange(true))
        return;
    if (thread_.joinable())
        thread_.join();
    cancel_.store(false);
    isHost_.store(true);
    mode_.store(ShareMode::Host);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key_.clear();
        importedReady_ = false;
    }
    setStatus(ShareState::Preparing, "Preparing to share...");
    thread_ = std::thread(&ProfileShareService::hostWorker, this, paths, &store, profileName, opts);
}

void ProfileShareService::startReceiving(const GamePaths& paths, ProfileStore& store,
                                         const std::string& keyText, const std::string& targetProfileId) {
    if (running_.exchange(true))
        return;
    cancel_.store(false);
    isHost_.store(false);
    mode_.store(ShareMode::Receive);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        importedReady_ = false;
    }
    setStatus(ShareState::Preparing, "Reading key...");
    thread_ = std::thread(&ProfileShareService::receiveWorker, this, paths, &store, keyText, targetProfileId);
}

void ProfileShareService::startExport(ProfileStore& store, const std::string& profileName,
                                      const std::filesystem::path& dest) {
    if (running_.exchange(true))
        return;
    if (thread_.joinable())
        thread_.join();
    cancel_.store(false);
    isHost_.store(true);   // reads the store, like a host; UI may still read concurrently
    mode_.store(ShareMode::Export);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key_.clear();
        importedReady_ = false;
    }
    setStatus(ShareState::Preparing, "Exporting profile...");
    thread_ = std::thread(&ProfileShareService::exportWorker, this, &store, profileName, dest);
}

void ProfileShareService::startImport(ProfileStore& store, const std::filesystem::path& src,
                                      const std::string& targetProfileId) {
    if (running_.exchange(true))
        return;
    if (thread_.joinable())
        thread_.join();
    cancel_.store(false);
    isHost_.store(false);  // writes the store, like a receive; UI must not read concurrently
    mode_.store(ShareMode::Import);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        importedReady_ = false;
    }
    setStatus(ShareState::Preparing, "Reading profile file...");
    thread_ = std::thread(&ProfileShareService::importWorker, this, &store, src, targetProfileId);
}

void ProfileShareService::stop() {
    cancel_.store(true);
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        if (g_session)
            g_session->close();   // interrupts a blocking accept/recv on the worker
    }
    if (thread_.joinable())
        thread_.join();
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        g_session.reset();
    }
    if (std::uint16_t p = mappedPort_.exchange(0); p != 0)
        net::unmap(p);
    running_.store(false);
    mode_.store(ShareMode::None);
    if (state_.load() != ShareState::Failed && state_.load() != ShareState::Done)
        state_.store(ShareState::Idle);
}

void ProfileShareService::hostWorker(GamePaths paths, ProfileStore* store, std::string profileName, HostOptions opts) {
    BundleManifest manifest;
    if (!gatherManifest(*store, profileName, manifest) || manifest.mods.empty()) {
        fail("This profile has no mods to share.");
        running_.store(false);
        return;
    }

    KeyPair keys;
    if (!generateKeyPair(keys)) {
        fail("Could not generate a key pair.");
        running_.store(false);
        return;
    }

    PeerSession* ps = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        g_session = std::make_unique<PeerSession>();
        ps = g_session.get();
    }

    std::string err;
    net::UpnpMapping m;
    std::uint16_t port = 0;
    if (opts.useUpnp) {
        setStatus(ShareState::MappingPort, "Opening router port via UPnP...");
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(kPortMin, kPortMax);
        std::string lastErr = "no UPnP gateway found";
        // Try a few random ports; a router may already have one mapped or in use.
        for (int attempt = 0; attempt < kPortAttempts && !cancel_.load() && port == 0; ++attempt) {
            const std::uint16_t candidate = static_cast<std::uint16_t>(dist(gen));
            net::UpnpMapping cand = net::map(candidate);
            if (!cand.ok) { lastErr = cand.error; continue; }
            if (ps->listen(candidate, err)) { m = cand; port = candidate; }
            else { lastErr = err; net::unmap(candidate); ps->close(); }
        }
        if (port == 0) {
            fail("Couldn't open a port for sharing after " + std::to_string(kPortAttempts) +
                 " attempts — UPnP must be enabled on your router. (" + lastErr + ")");
            running_.store(false);
            return;
        }
        mappedPort_.store(port);   // owned mapping; stop() unmaps it
    } else {
        // Manual port forwarding: bind the user's pre-forwarded port and publish the WAN IP
        // (typed override or auto-detected). No UPnP mapping to own or tear down.
        if (opts.fixedPort == 0) {
            fail("Enter the TCP port you forwarded on your router.");
            running_.store(false);
            return;
        }
        setStatus(ShareState::Preparing, "Starting listener...");
        if (!ps->listen(opts.fixedPort, err)) {
            fail("Couldn't listen on port " + std::to_string(opts.fixedPort) + " — " + err);
            running_.store(false);
            return;
        }
        std::string ip = trim(opts.ipOverride);
        if (ip.empty()) {
            setStatus(ShareState::Preparing, "Detecting your public IP...");
            auto detected = detectPublicIp();
            if (!detected) {
                fail("Couldn't detect your public IP — enter it manually.");
                running_.store(false);
                return;
            }
            ip = *detected;
        } else if (!looksLikeIpv4(ip)) {
            fail("'" + ip + "' is not a valid IPv4 address.");
            running_.store(false);
            return;
        }
        m.externalIp = ip;
        port = opts.fixedPort;
    }

    ConnectionKeyData kd;
    kd.externalIp = m.externalIp;
    kd.port = port;
    kd.publicKey = keys.publicKey;
    std::string key = encodeConnectionKey(kd);
    if (key.empty()) {
        fail("Could not encode the connection key.");
        if (opts.useUpnp) net::unmap(port);
        mappedPort_.store(0);
        running_.store(false);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key_ = key;
    }

    setStatus(ShareState::WaitingForPeer, "Waiting for a peer. Share your key.");

    const std::uint64_t totalFiles = [&] {
        std::uint64_t n = 0;
        for (const auto& mod : manifest.mods) n += mod.files.size();
        return n == 0 ? 1 : n;
    }();
    const std::string manifestJson = manifestToJson(manifest);

    while (!cancel_.load()) {
        PeerSession::Accepted a = ps->accept(1000, err);
        if (a == PeerSession::Accepted::TimedOut)
            continue;
        if (a == PeerSession::Accepted::Error) {
            if (!cancel_.load())
                fail("Listener error: " + err);
            break;
        }

        // Serve one peer. A failure here drops just this transfer; keep hosting.
        progress_.store(0.0f);
        setStatus(ShareState::Transferring, "Sending profile...");
        bool ok = true;

        Bytes greeting(kGreeting.begin(), kGreeting.end());
        Bytes nonce, sig;
        if (ok) ok = ps->sendFrame(greeting, err);
        if (ok) ok = ps->recvFrame(nonce, kMaxHandshakeFrame, err) && nonce.size() == 32;
        if (ok) {
            sig = sign(keys.privateKey, nonce);
            ok = !sig.empty() && ps->sendFrame(sig, err);
        }
        if (ok) ok = ps->sendFrame(Bytes(manifestJson.begin(), manifestJson.end()), err);

        std::uint64_t done = 0;
        for (std::size_t mi = 0; ok && mi < manifest.mods.size() && !cancel_.load(); ++mi) {
            std::vector<Bytes> files;
            if (!readModBytes(*store, manifest, mi, files)) {
                err = "Could not read mod files."; ok = false; break;
            }
            for (const Bytes& raw : files) {
                Bytes payload;
                putU64(payload, raw.size());
                if (!raw.empty()) {
                    Bytes comp = lzmaCompress(raw, 3);   // smaller P2P payload; ratio over speed
                    if (comp.empty()) { err = "Compression failed."; ok = false; break; }
                    payload.insert(payload.end(), comp.begin(), comp.end());
                }
                if (!ps->sendFrame(payload, err)) { ok = false; break; }
                progress_.store(static_cast<float>(++done) / static_cast<float>(totalFiles));
            }
        }

        ps->closeConnection();
        if (ok)
            setStatus(ShareState::WaitingForPeer, "Profile sent. Waiting for another peer...");
        else if (!cancel_.load())
            setStatus(ShareState::WaitingForPeer, "A transfer failed (" + err + "). Still waiting...");
    }

    if (opts.useUpnp) net::unmap(port);
    mappedPort_.store(0);
    running_.store(false);
}

void ProfileShareService::receiveWorker(GamePaths paths, ProfileStore* store, std::string keyText, std::string profileId) {
    auto kd = parseConnectionKey(keyText);
    if (!kd) {
        fail("Invalid connection key.");
        running_.store(false);
        return;
    }

    PeerSession* ps = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        g_session = std::make_unique<PeerSession>();
        ps = g_session.get();
    }

    setStatus(ShareState::Preparing, "Connecting to host...");
    std::string err;
    if (!ps->connectTo(kd->externalIp, kd->port, 10000, err)) {
        fail("Could not connect: " + err);
        running_.store(false);
        return;
    }

    Bytes greeting;
    if (!ps->recvFrame(greeting, kMaxHandshakeFrame, err) ||
        greeting.size() != kGreeting.size() ||
        !std::equal(kGreeting.begin(), kGreeting.end(), greeting.begin())) {
        fail("This host is not an S2ModManager peer.");
        running_.store(false);
        return;
    }

    setStatus(ShareState::Authenticating, "Verifying host identity...");
    std::vector<std::uint8_t> nonce(32);
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        fail("Could not generate a challenge.");
        running_.store(false);
        return;
    }
    Bytes sig;
    if (!ps->sendFrame(nonce, err) || !ps->recvFrame(sig, kMaxHandshakeFrame, err)) {
        fail("Handshake failed: " + err);
        running_.store(false);
        return;
    }
    if (!verify(kd->publicKey, nonce, sig)) {
        fail("Host authentication failed — the key does not match this host.");
        running_.store(false);
        return;
    }

    setStatus(ShareState::Transferring, "Receiving profile...");
    Bytes manifestBytes;
    if (!ps->recvFrame(manifestBytes, kMaxManifestFrame, err)) {
        fail("Failed to read the profile manifest: " + err);
        running_.store(false);
        return;
    }
    auto manifest = manifestFromJson(std::string(manifestBytes.begin(), manifestBytes.end()));
    if (!manifest || manifest->mods.empty()) {
        fail("The host sent an invalid profile.");
        running_.store(false);
        return;
    }

    const std::uint64_t totalFiles = [&] {
        std::uint64_t n = 0;
        for (const auto& mod : manifest->mods) n += mod.files.size();
        return n == 0 ? 1 : n;
    }();
    std::uint64_t done = 0;

    for (const BundleMod& mod : manifest->mods) {
        if (cancel_.load()) { fail("Cancelled."); running_.store(false); return; }
        std::vector<Bytes> files;
        for (const BundleFileMeta& fm : mod.files) {
            Bytes payload;
            if (!ps->recvFrame(payload, err) || payload.size() < 8) {
                fail("Transfer interrupted: " + err);
                running_.store(false);
                return;
            }
            std::uint64_t rawSize = getU64(payload.data());
            if (rawSize > 4ull * 1024 * 1024 * 1024) {
                fail("The host sent an implausibly large file.");
                running_.store(false);
                return;
            }
            Bytes raw;
            if (rawSize > 0) {
                Bytes comp(payload.begin() + 8, payload.end());
                if (!lzmaDecompress(comp, static_cast<std::size_t>(rawSize), raw)) {
                    fail("Failed to decompress '" + fm.relPath + "'.");
                    running_.store(false);
                    return;
                }
            }
            files.push_back(std::move(raw));
            progress_.store(static_cast<float>(++done) / static_cast<float>(totalFiles));
        }
        if (!writeMod(*store, profileId, mod, files)) {
            fail("Failed to install mod '" + mod.name + "'.");
            running_.store(false);
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        importedProfile_ = manifest->profileName;
        importedReady_ = true;
    }
    setStatus(ShareState::Done, "Imported profile '" + manifest->profileName + "'.");
    running_.store(false);
}

void ProfileShareService::exportWorker(ProfileStore* store, std::string profileName, std::filesystem::path dest) {
    setStatus(ShareState::Transferring, "Writing '" + dest.filename().string() + "'...");
    auto onProgress = [this](float f) { progress_.store(f); };
    if (!exportProfile(*store, profileName, dest, onProgress)) {
        fail("Couldn't export this profile (it may have no mods, or the file isn't writable).");
        running_.store(false);
        return;
    }
    setStatus(ShareState::Done, "Exported to " + dest.filename().string() + ".");
    running_.store(false);
}

void ProfileShareService::importWorker(ProfileStore* store, std::filesystem::path src, std::string profileId) {
    setStatus(ShareState::Transferring, "Importing from " + src.filename().string() + "...");
    auto onProgress = [this](float f) { progress_.store(f); };
    auto name = importProfile(*store, profileId, src, onProgress);
    if (!name) {
        fail("Not a valid S2 profile file.");
        running_.store(false);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        importedProfile_ = *name;
        importedReady_ = true;
    }
    setStatus(ShareState::Done, "Imported profile '" + *name + "'.");
    running_.store(false);
}

}  // namespace core
