#include "Compression.h"
#include <array>
#include <thread>
#include <lzma.h>
#include <zstd.h>

namespace core {

namespace {
// Worker threads to throw at (de)compression, capped so we don't oversubscribe.
std::uint32_t lzmaThreads() {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw <= 1) return 1;
    return hw > 16 ? 16u : static_cast<std::uint32_t>(hw);
}

// Drives a configured lzma_stream over an in-memory buffer to completion, growing out as needed.
bool runStream(lzma_stream& strm, const Bytes& in, Bytes& out) {
    if (out.empty())
        out.resize(in.size() ? in.size() : 1);
    strm.next_in = in.data();
    strm.avail_in = in.size();
    strm.next_out = out.data();
    strm.avail_out = out.size();
    for (;;) {
        lzma_ret r = lzma_code(&strm, LZMA_FINISH);
        if (r == LZMA_STREAM_END) {
            out.resize(out.size() - strm.avail_out);
            return true;
        }
        if (r != LZMA_OK)
            return false;
        if (strm.avail_out == 0) {
            const std::size_t used = out.size();
            out.resize(out.size() * 2);
            strm.next_out = out.data() + used;
            strm.avail_out = out.size() - used;
        }
    }
}
}

// Below this size MT just adds thread/buffer overhead (each block needs ~3x dict of buffers),
// so the one-shot single-thread path wins — and manifest.json is written on every mutation.
constexpr std::size_t kMtThreshold = 2ULL * 1024 * 1024;

Bytes lzmaCompress(const Bytes& in, std::uint32_t preset) {
    const std::uint32_t threads = lzmaThreads();
    if (threads <= 1 || in.size() < kMtThreshold) {
        Bytes out(lzma_stream_buffer_bound(in.size()));
        std::size_t outPos = 0;
        if (lzma_easy_buffer_encode(preset, LZMA_CHECK_CRC32, nullptr,
                in.data(), in.size(), out.data(), &outPos, out.size()) != LZMA_OK)
            return {};
        out.resize(outPos);
        return out;
    }

    lzma_mt mt = {};
    mt.threads = threads;
    mt.preset = preset;
    mt.check = LZMA_CHECK_CRC32;
    // block_size 0 lets liblzma pick (~3x dict) so large inputs split into many parallel blocks.
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_stream_encoder_mt(&strm, &mt) != LZMA_OK)
        return {};
    Bytes out(lzma_stream_buffer_bound(in.size()));
    const bool ok = runStream(strm, in, out);
    lzma_end(&strm);
    return ok ? out : Bytes{};
}

bool lzmaDecompress(const Bytes& in, std::size_t rawSize, Bytes& out) {
    constexpr std::size_t kMaxRaw = 4ULL * 1024 * 1024 * 1024;
    if (rawSize > kMaxRaw)
        return false;
    out.assign(rawSize, 0);

    const std::uint32_t threads = lzmaThreads();
    if (threads <= 1 || rawSize < kMtThreshold) {
        std::uint64_t memlimit = 128ULL * 1024 * 1024;
        std::size_t inPos = 0, outPos = 0;
        lzma_ret r = lzma_stream_buffer_decode(&memlimit, 0, nullptr,
            in.data(), &inPos, in.size(), out.data(), &outPos, out.size());
        if (r == LZMA_OK && outPos == rawSize)
            return true;
        out.clear();
        return false;
    }

    lzma_mt mt = {};
    mt.threads = threads;
    mt.memlimit_threading = 256ULL * 1024 * 1024;   // per-run budget before falling back to 1 thread
    mt.memlimit_stop = UINT64_MAX;                  // size already bounded by rawSize/kMaxRaw
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_stream_decoder_mt(&strm, &mt) != LZMA_OK) {
        out.clear();
        return false;
    }
    const bool ok = runStream(strm, in, out) && strm.total_out == rawSize;
    lzma_end(&strm);
    if (!ok)
        out.clear();
    return ok;
}

Bytes zstdCompress(const Bytes& in, int level) {
    Bytes out(ZSTD_compressBound(in.size()));
    std::size_t n;
    const std::uint32_t threads = lzmaThreads();
    if (threads > 1 && in.size() >= kMtThreshold) {
        ZSTD_CCtx* c = ZSTD_createCCtx();
        if (!c)
            return {};
        ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level);
        ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, static_cast<int>(threads));   // no-op if zstd built w/o MT
        n = ZSTD_compress2(c, out.data(), out.size(), in.data(), in.size());
        ZSTD_freeCCtx(c);
    } else {
        n = ZSTD_compress(out.data(), out.size(), in.data(), in.size(), level);
    }
    if (ZSTD_isError(n))
        return {};
    out.resize(n);
    return out;
}

bool zstdDecompress(const Bytes& in, std::size_t rawSize, Bytes& out) {
    constexpr std::size_t kMaxRaw = 4ULL * 1024 * 1024 * 1024;
    if (rawSize > kMaxRaw)
        return false;
    out.assign(rawSize, 0);
    const std::size_t n = ZSTD_decompress(out.data(), out.size(), in.data(), in.size());
    if (ZSTD_isError(n) || n != rawSize) {
        out.clear();
        return false;
    }
    return true;
}

namespace {
constexpr auto makeCrc32Table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}
constexpr auto kCrc32Table = makeCrc32Table();
}

std::uint32_t crc32(const Bytes& in) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint8_t b : in)
        c = kCrc32Table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}
