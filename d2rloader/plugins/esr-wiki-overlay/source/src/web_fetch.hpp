// Small WinHTTP and file helpers shared by the website updaters (Windows only).
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>

#include <string>

namespace esrwiki {

std::wstring Wide(const std::string& text);
std::string Narrow(const std::wstring& text);
bool ReadTextFile(const std::wstring& path, std::string& out);
// Writes through a temporary file, so a crash never leaves half a page behind.
bool WriteFileAtomic(const std::wstring& path, const std::string& data);

struct Meta {
    std::string etag;
    std::string lastModified;
};

struct FetchResult {
    enum class Kind { Failed, NotModified, Page } kind = Kind::Failed;
    std::string body;
    Meta meta;
    std::string error;
};

// A conditional GET: nothing is downloaded when `known` still matches the site's copy. `stop` is an event
// that cuts a long download short.
FetchResult Fetch(const std::wstring& url, const Meta& known, HANDLE stop,
                  const wchar_t* userAgent = L"ESR-Wiki-Overlay/1.4 (cube recipes)");

}  // namespace esrwiki
