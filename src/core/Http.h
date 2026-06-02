#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace core {

/// <summary>Performs a GET request, returning the response body in memory.</summary>
/// <param name="url">Absolute http(s) URL. Redirects are followed.</param>
/// <returns>The response body on HTTP 2xx, or nullopt on transport/HTTP failure.</returns>
[[nodiscard]] std::optional<std::string> httpGet(const std::string& url);

/// <summary>Downloads a URL to a file, reporting progress.</summary>
/// <param name="url">Absolute http(s) URL. Redirects are followed.</param>
/// <param name="dest">Destination file (created/overwritten; parent dirs must exist).</param>
/// <param name="onProgress">Called with a 0..1 fraction (or 0 when the size is unknown). May be empty.</param>
/// <returns>True only when the file was written and the server returned HTTP 2xx.</returns>
[[nodiscard]] bool downloadFile(const std::string& url, const std::filesystem::path& dest,
                                const std::function<void(float)>& onProgress);

}
