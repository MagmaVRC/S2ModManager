#include "Archive.h"
#include "Paths.h"
#include <archive.h>
#include <archive_entry.h>

namespace core {

namespace {

/// <summary>Normalizes an archive member path to a safe relative path under the destination.
/// Returns an empty path if the member is absolute or escapes via "..".</summary>
std::filesystem::path safeRelative(const std::wstring& member) {
    std::filesystem::path rel(member);
    if (rel.is_absolute() || rel.has_root_name())
        return {};

    std::filesystem::path cleaned;
    for (const auto& part : rel) {
        const std::wstring s = part.wstring();
        if (s == L"." || s.empty())
            continue;
        if (s == L"..")
            return {};
        cleaned /= part;
    }
    return cleaned;
}

bool copyData(archive* in, archive* out) {
    const void* buff;
    std::size_t size;
    la_int64_t offset;
    for (;;) {
        int r = archive_read_data_block(in, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return true;
        if (r < ARCHIVE_WARN)
            return false;
        if (archive_write_data_block(out, buff, size, offset) < ARCHIVE_WARN)
            return false;
    }
}

}

bool extract(const std::filesystem::path& archivePath,
             const std::filesystem::path& destDir,
             std::vector<std::string>* outEntries) {
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    archive* in = archive_read_new();
    archive_read_support_format_all(in);
    archive_read_support_filter_all(in);

    archive* out = archive_write_disk_new();
    archive_write_disk_set_options(out,
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    archive_write_disk_set_standard_lookup(out);

    bool ok = false;
    if (archive_read_open_filename_w(in, archivePath.wstring().c_str(), 1 << 16) == ARCHIVE_OK) {
        int wrote = 0;
        bool failed = false;
        archive_entry* entry = nullptr;
        while (archive_read_next_header(in, &entry) == ARCHIVE_OK) {
            const wchar_t* raw = archive_entry_pathname_w(entry);
            std::filesystem::path rel = raw ? safeRelative(raw) : std::filesystem::path{};
            if (rel.empty())
                continue;

            const std::filesystem::path full = destDir / rel;
            archive_entry_copy_pathname_w(entry, full.wstring().c_str());

            if (archive_write_header(out, entry) < ARCHIVE_OK)
                continue;
            if (archive_entry_size(entry) > 0 && !copyData(in, out)) {
                failed = true;
                break;
            }
            archive_write_finish_entry(out);

            if (archive_entry_filetype(entry) == AE_IFREG) {
                ++wrote;
                if (outEntries)
                    outEntries->push_back(narrow(rel.wstring()));
            }
        }
        ok = !failed && wrote > 0;
    }

    archive_read_free(in);
    archive_write_free(out);
    return ok;
}

}
