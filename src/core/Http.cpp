#include "Http.h"
#include <curl/curl.h>
#include <fstream>
#include <mutex>

namespace core {

namespace {

constexpr const char* kUserAgent = "S2ModManager/0.1 (+https://github.com/Magma/S2ModManager)";

void ensureGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t appendToString(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::size_t writeToStream(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return out->good() ? size * nmemb : 0;
}

int progressBridge(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* fn = static_cast<const std::function<void(float)>*>(userdata);
    if (*fn)
        (*fn)(dltotal > 0 ? static_cast<float>(static_cast<double>(dlnow) / static_cast<double>(dltotal)) : 0.0f);
    return 0;
}

void applyCommonOptions(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
}

}

std::optional<std::string> httpGet(const std::string& url) {
    ensureGlobalInit();
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string body;
    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   // "" enables all built-in decompression

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK)
        return std::nullopt;
    return body;
}

bool downloadFile(const std::string& url, const std::filesystem::path& dest,
                  const std::function<void(float)>& onProgress) {
    ensureGlobalInit();
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out)
        { curl_easy_cleanup(curl); return false; }

    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressBridge);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &onProgress);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    out.close();

    if (rc != CURLE_OK) {
        std::error_code ec;
        std::filesystem::remove(dest, ec);
        return false;
    }
    return true;
}

}
