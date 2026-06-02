#pragma once
#include <cstddef>
#include <filesystem>
#include <vector>

namespace platform {

/// <summary>Loads an embedded RT_RCDATA resource into memory.</summary>
/// <param name="resourceId">Numeric resource id (see resources/resource.h).</param>
/// <returns>The resource bytes, or an empty vector if the resource is missing/empty.</returns>
[[nodiscard]] std::vector<unsigned char> readEmbeddedResource(int resourceId);

/// <summary>Writes an embedded RT_RCDATA resource to <paramref name="dest"/> if the file
/// is missing or empty, creating parent directories as needed.</summary>
/// <param name="resourceId">Numeric resource id (see resources/resource.h).</param>
/// <param name="dest">Destination file path.</param>
/// <returns>True if the file exists on return (already present or written successfully).</returns>
bool extractEmbeddedResource(int resourceId, const std::filesystem::path& dest);

}
