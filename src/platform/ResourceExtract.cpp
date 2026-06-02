#include "ResourceExtract.h"
#include <windows.h>
#include <fstream>
#include <system_error>

namespace platform {

std::vector<unsigned char> readEmbeddedResource(int resourceId) {
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res)
        return {};
    HGLOBAL handle = LoadResource(mod, res);
    if (!handle)
        return {};
    const void* data = LockResource(handle);
    DWORD size = SizeofResource(mod, res);
    if (!data || size == 0)
        return {};
    const auto* p = static_cast<const unsigned char*>(data);
    return std::vector<unsigned char>(p, p + size);
}

bool extractEmbeddedResource(int resourceId, const std::filesystem::path& dest) {
    std::error_code ec;
    if (std::filesystem::exists(dest, ec) && std::filesystem::file_size(dest, ec) > 0)
        return true;

    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res)
        return false;
    HGLOBAL handle = LoadResource(mod, res);
    if (!handle)
        return false;
    const void* data = LockResource(handle);
    DWORD size = SizeofResource(mod, res);
    if (!data || size == 0)
        return false;

    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

}
