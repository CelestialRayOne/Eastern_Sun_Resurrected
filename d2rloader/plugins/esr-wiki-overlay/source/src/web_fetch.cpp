#include "web_fetch.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace esrwiki {

namespace {
constexpr std::size_t kMaxPageBytes = 32u * 1024u * 1024u;  // the suffixes page alone is about 14 MB

std::wstring QueryHeader(HINTERNET request, DWORD what) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, what, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, what, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
    value.resize(size / sizeof(wchar_t));
    return value;
}

}  // namespace

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream content;
    content << file.rdbuf();
    out = content.str();
    return true;
}

// Writes through a temporary file, so a crash never leaves half a page behind.
bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

FetchResult Fetch(const std::wstring& url, const Meta& known, HANDLE stop, const wchar_t* userAgent) {
    FetchResult result;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        result.error = "bad URL";
        return result;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        // Older Windows has no automatic proxy mode.
        session = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == nullptr) {
        result.error = "WinHttpOpen failed";
        return result;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection == nullptr
                            ? nullptr
                            : WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    auto closeAll = [&]() {
        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
    };
    if (request == nullptr) {
        result.error = "could not open the request";
        closeAll();
        return result;
    }

    std::wstring headers;
    if (!known.etag.empty()) headers += L"If-None-Match: " + Wide(known.etag) + L"\r\n";
    if (!known.lastModified.empty()) headers += L"If-Modified-Since: " + Wide(known.lastModified) + L"\r\n";
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        result.error = "no response from the site (error " + std::to_string(GetLastError()) + ")";
        closeAll();
        return result;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status == 304) {
        result.kind = FetchResult::Kind::NotModified;
        closeAll();
        return result;
    }
    if (status != 200) {
        result.error = "the site answered HTTP " + std::to_string(status);
        closeAll();
        return result;
    }
    result.meta.etag = Narrow(QueryHeader(request, WINHTTP_QUERY_ETAG));
    result.meta.lastModified = Narrow(QueryHeader(request, WINHTTP_QUERY_LAST_MODIFIED));

    std::vector<char> chunk(64 * 1024);
    for (;;) {
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) {
            result.error = "stopped";
            closeAll();
            return result;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
            result.error = "the download was interrupted";
            result.body.clear();
            closeAll();
            return result;
        }
        if (read == 0) break;
        result.body.append(chunk.data(), read);
        if (result.body.size() > kMaxPageBytes) {
            result.error = "the page is larger than expected";
            result.body.clear();
            closeAll();
            return result;
        }
    }
    closeAll();
    result.kind = FetchResult::Kind::Page;
    return result;
}


}  // namespace esrwiki
