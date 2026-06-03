#include "Vfs.h"
#include <fstream>
#include <cstring>
#include <windows.h>

namespace core {

namespace {
constexpr char kMagic[4] = { 'S', '2', 'M', 'M' };
constexpr std::uint32_t kVersion = 1;

template <class T> void put(std::ofstream& o, const T& v) {
    o.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T> bool get(std::ifstream& i, T& v) {
    return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

bool globMatch(const char* pat, const char* str) {
    const char* sp = nullptr;
    const char* ss = nullptr;
    while (*str) {
        if (*pat == *str || *pat == '?') { ++pat; ++str; }
        else if (*pat == '*') { sp = pat++; ss = str; }
        else if (sp) { pat = sp + 1; str = ++ss; }
        else return false;
    }
    while (*pat == '*') ++pat;
    return *pat == '\0';
}
}

bool Vfs::open(const std::filesystem::path& file) {
    file_ = file;
    entries_.clear();
    dirty_ = false;

    std::ifstream in(file_, std::ios::binary);
    if (!in)
        return true;

    char magic[4];
    if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0)
        return false;

    std::uint32_t version = 0;
    std::uint64_t indexOffset = 0;
    std::uint32_t count = 0;
    if (!get(in, version) || !get(in, indexOffset) || !get(in, count) || version != kVersion)
        return false;

    if (count > 100000)
        return false;
    in.seekg(0, std::ios::end);
    std::uint64_t fileSize = static_cast<std::uint64_t>(in.tellg());
    if (indexOffset > fileSize)
        return false;

    in.seekg(static_cast<std::streamoff>(indexOffset));
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t plen = 0;
        if (!get(in, plen))
            return false;
        std::string path(plen, '\0');
        if (plen && !in.read(path.data(), plen))
            return false;

        Entry e;
        std::uint8_t comp = 0;
        if (!get(in, comp) || !get(in, e.rawSize) || !get(in, e.compSize) ||
            !get(in, e.crc) || !get(in, e.offset))
            return false;
        e.compression = comp;
        e.onDisk = true;
        if (e.offset > indexOffset || e.compSize > indexOffset - e.offset ||
            e.rawSize > 4ULL * 1024 * 1024 * 1024)
            return false;
        entries_[path] = std::move(e);
    }
    return true;
}

bool Vfs::has(const std::string& path) const {
    return entries_.contains(path);
}

bool Vfs::readCompressed(const Entry& e, Bytes& comp) const {
    if (!e.onDisk) {
        comp = e.blob;
        return true;
    }
    std::ifstream in(file_, std::ios::binary);
    if (!in)
        return false;
    in.seekg(static_cast<std::streamoff>(e.offset));
    comp.resize(static_cast<std::size_t>(e.compSize));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(comp.data()),
                                     static_cast<std::streamsize>(e.compSize)));
}

bool Vfs::read(const std::string& path, Bytes& out) const {
    auto it = entries_.find(path);
    if (it == entries_.end())
        return false;
    const Entry& e = it->second;

    Bytes comp;
    if (!readCompressed(e, comp))
        return false;

    const std::size_t raw = static_cast<std::size_t>(e.rawSize);
    if (e.compression == 0)
        out = std::move(comp);                              // stored (incompressible)
    else if (e.compression == 1) {                          // legacy LZMA blobs
        if (!lzmaDecompress(comp, raw, out)) return false;
    } else if (e.compression == 2) {                        // zstd
        if (!zstdDecompress(comp, raw, out)) return false;
    } else {
        return false;
    }

    return crc32(out) == e.crc;
}

bool Vfs::readText(const std::string& path, std::string& out) const {
    Bytes b;
    if (!read(path, b))
        return false;
    out.assign(b.begin(), b.end());
    return true;
}

void Vfs::write(const std::string& path, const Bytes& data) {
    Entry e;
    e.rawSize = data.size();
    e.crc = crc32(data);
    e.onDisk = false;

    Bytes comp = zstdCompress(data);
    if (!comp.empty() && comp.size() < data.size()) {
        e.compression = 2;   // zstd
        e.blob = std::move(comp);
    } else {
        e.compression = 0;   // stored: incompressible (e.g. already-compressed paks)
        e.blob = data;
    }
    e.compSize = e.blob.size();

    entries_[path] = std::move(e);
    dirty_ = true;
}

void Vfs::writeText(const std::string& path, const std::string& text) {
    write(path, Bytes(text.begin(), text.end()));
}

void Vfs::remove(const std::string& path) {
    if (entries_.erase(path))
        dirty_ = true;
}

void Vfs::removePrefix(const std::string& prefix) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.starts_with(prefix)) {
            it = entries_.erase(it);
            dirty_ = true;
        } else {
            ++it;
        }
    }
}

std::vector<std::string> Vfs::list(const std::string& glob) const {
    std::vector<std::string> out;
    for (const auto& [path, e] : entries_)
        if (globMatch(glob.c_str(), path.c_str()))
            out.push_back(path);
    return out;
}

bool Vfs::commit() {
    if (!dirty_)
        return true;

    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);

    std::filesystem::path tmp = file_;
    tmp += L".tmp";

    struct TmpGuard {
        std::filesystem::path& p;
        bool committed = false;
        ~TmpGuard() { if (!committed) { std::error_code e; std::filesystem::remove(p, e); } }
    } guard{tmp};

    std::map<std::string, std::uint64_t> newOffsets;

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        out.write(kMagic, 4);
        put(out, kVersion);
        std::uint64_t indexOffset = 0;
        put(out, indexOffset);
        std::uint32_t count = static_cast<std::uint32_t>(entries_.size());
        put(out, count);

        for (auto& [path, e] : entries_) {
            Bytes comp;
            if (!readCompressed(e, comp))
                return false;
            newOffsets[path] = static_cast<std::uint64_t>(out.tellp());
            if (!comp.empty())
                out.write(reinterpret_cast<const char*>(comp.data()),
                          static_cast<std::streamsize>(comp.size()));
        }

        std::uint64_t idx = static_cast<std::uint64_t>(out.tellp());
        for (auto& [path, e] : entries_) {
            if (path.size() > 65535) return false;
            std::uint16_t plen = static_cast<std::uint16_t>(path.size());
            put(out, plen);
            out.write(path.data(), static_cast<std::streamsize>(plen));
            put(out, e.compression);
            put(out, e.rawSize);
            put(out, e.compSize);
            put(out, e.crc);
            put(out, newOffsets[path]);
        }

        out.seekp(8, std::ios::beg);
        put(out, idx);
        out.flush();
        if (out.fail())
            return false;
    }

    if (!MoveFileExW(tmp.c_str(), file_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return false;

    guard.committed = true;

    for (auto& [path, e] : entries_) {
        e.onDisk = true;
        e.offset = newOffsets[path];
        e.blob.clear();
        e.blob.shrink_to_fit();
    }
    dirty_ = false;
    return true;
}

}
