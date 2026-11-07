#include "cube_updater.hpp"

#include <atomic>
#include <fstream>
#include <sstream>
#include <vector>

#include "cube_live.hpp"
#include "web_fetch.hpp"

namespace esrwiki {

namespace {

constexpr DWORD kRecheckMs = 6u * 60u * 60u * 1000u;  // ask the site again every six hours
constexpr wchar_t kPageFile[] = L"cube-recipes-page.html";
constexpr wchar_t kMetaFile[] = L"cube-recipes-page.meta";
constexpr wchar_t kOptOutFile[] = L"live-updates-disabled.txt";  // the one switch for every website update

}  // namespace

struct CubeUpdaterState {
    std::wstring cacheDir;
    std::wstring url;
    std::function<void(const std::string&)> apply;
    std::function<void(const std::string&)> log;
    HANDLE stop = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> cancelled{false};  // set when stopping, so a late page is not handed on

    void Log(const std::string& message) const {
        if (log && !cancelled) log("ESR Wiki cube recipes: " + message);
    }

    // Builds wiki data from a page and passes it on. False when the page is not usable.
    bool Use(const std::string& page, const char* source) {
        if (cancelled) return false;
        std::string wikiData, error;
        if (!BuildCubeWikiData(page, wikiData, error)) {
            Log(std::string("the ") + source + " page was not used: " + error);
            return false;
        }
        if (cancelled) return false;
        apply(wikiData);
        return true;
    }

    void Run() {
        const std::wstring pagePath = cacheDir + L"\\" + kPageFile;
        const std::wstring metaPath = cacheDir + L"\\" + kMetaFile;
        Meta known;
        std::string cached;
        if (ReadTextFile(pagePath, cached)) {
            std::string metaText;
            if (ReadTextFile(metaPath, metaText)) {
                std::istringstream lines(metaText);
                std::getline(lines, known.etag);
                std::getline(lines, known.lastModified);
                for (std::string* value : {&known.etag, &known.lastModified}) {
                    while (!value->empty() && (value->back() == '\r' || value->back() == '\n')) value->pop_back();
                }
            }
            if (Use(cached, "saved")) {
                Log("using the copy saved from the website");
            } else {
                known = Meta{};  // the saved copy is no good, so ask for the page afresh
                cached.clear();
            }
        }
        for (;;) {
            if (cancelled) return;
            const FetchResult fetched = Fetch(url, cached.empty() ? Meta{} : known, stop);
            if (fetched.kind == FetchResult::Kind::NotModified) {
                Log("the website page is unchanged");
            } else if (fetched.kind == FetchResult::Kind::Page) {
                if (Use(fetched.body, "downloaded")) {
                    cached = fetched.body;
                    known = fetched.meta;
                    if (!WriteFileAtomic(pagePath, fetched.body)) Log("could not save a copy of the page");
                    else WriteFileAtomic(metaPath, known.etag + "\n" + known.lastModified + "\n");
                    Log("updated from the website");
                }
            } else {
                Log("could not check the website: " + fetched.error);
            }
            if (cancelled || WaitForSingleObject(stop, kRecheckMs) == WAIT_OBJECT_0 || cancelled) return;
        }
    }
};

namespace {
DWORD WINAPI UpdaterThread(LPVOID parameter) {
    static_cast<CubeUpdaterState*>(parameter)->Run();
    return 0;
}
}  // namespace

CubeUpdater::CubeUpdater() : state_(std::make_unique<CubeUpdaterState>()) {}

CubeUpdater::~CubeUpdater() { Stop(); }

void CubeUpdater::Start(const std::wstring& cacheDir, const std::wstring& url,
                        std::function<void(const std::string&)> apply, std::function<void(const std::string&)> log) {
    if (!state_ || state_->thread != nullptr) return;
    state_->cacheDir = cacheDir;
    state_->url = url;
    state_->apply = std::move(apply);
    state_->log = std::move(log);
    // A file with this name next to the data turns the website check off.
    if (GetFileAttributesW((cacheDir + L"\\" + kOptOutFile).c_str()) != INVALID_FILE_ATTRIBUTES) {
        state_->Log("website updates are turned off (live-updates-disabled.txt)");
        return;
    }
    state_->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state_->stop == nullptr) return;
    state_->thread = CreateThread(nullptr, 0, UpdaterThread, state_.get(), 0, nullptr);
}

void CubeUpdater::Stop() {
    if (!state_) return;  // abandoned during a slow download
    if (state_->thread != nullptr) {
        state_->cancelled = true;
        SetEvent(state_->stop);
        if (WaitForSingleObject(state_->thread, 3000) != WAIT_OBJECT_0) {
            // Still inside a slow download. Leave its state allocated; it ends on its own and hands nothing on.
            CloseHandle(state_->thread);
            state_->thread = nullptr;
            (void)state_.release();
            return;
        }
        CloseHandle(state_->thread);
        state_->thread = nullptr;
    }
    if (state_->stop != nullptr) {
        CloseHandle(state_->stop);
        state_->stop = nullptr;
    }
}

}  // namespace esrwiki
