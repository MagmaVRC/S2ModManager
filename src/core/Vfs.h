#pragma once
#include "Compression.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace core {

/// <summary>Single-file packed virtual filesystem (Data.dat) with LZMA-compressed,
/// glob-addressable entries.</summary>
class Vfs {
public:
    /// <summary>Opens an existing store or begins an empty one.</summary>
    /// <param name="file">Path to the Data.dat container.</param>
    /// <returns>False only when an existing file is present but corrupt or unreadable.</returns>
    [[nodiscard]] bool open(const std::filesystem::path& file);

    /// <summary>Returns whether an entry exists.</summary>
    [[nodiscard]] bool has(const std::string& path) const;

    /// <summary>Reads and decompresses an entry, verifying its CRC.</summary>
    [[nodiscard]] bool read(const std::string& path, Bytes& out) const;

    /// <summary>Reads an entry as UTF-8 text.</summary>
    [[nodiscard]] bool readText(const std::string& path, std::string& out) const;

    /// <summary>Compresses and stores an entry, held in memory until <see cref="commit"/>.</summary>
    void write(const std::string& path, const Bytes& data);

    /// <summary>Stores UTF-8 text.</summary>
    void writeText(const std::string& path, const std::string& text);

    /// <summary>Copies an entry's compressed blob to a new path without decompressing.</summary>
    bool copyEntry(const std::string& src, const std::string& dst);

    /// <summary>Removes an entry.</summary>
    void remove(const std::string& path);

    /// <summary>Removes every entry whose path starts with the given prefix.</summary>
    void removePrefix(const std::string& prefix);

    /// <summary>Lists entry paths matching a glob pattern (supports * and ?).</summary>
    [[nodiscard]] std::vector<std::string> list(const std::string& glob = "*") const;

    /// <summary>Flushes all pending changes to disk atomically.</summary>
    bool commit();

private:
    struct Entry {
        std::uint8_t  compression = 0;
        std::uint64_t rawSize = 0;
        std::uint64_t compSize = 0;
        std::uint32_t crc = 0;
        std::uint64_t offset = 0;
        bool          onDisk = false;
        Bytes         blob;
    };

    bool readCompressed(const Entry& e, Bytes& comp) const;

    std::filesystem::path file_;
    std::map<std::string, Entry> entries_;
    bool dirty_ = false;
};

}
