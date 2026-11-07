#include "web_pages_updater.hpp"

#include <atomic>
#include <sstream>
#include <vector>

#include "web_fetch.hpp"

namespace esrwiki {

namespace {

constexpr DWORD kRecheckMs = 6u * 60u * 60u * 1000u;  // ask the site again every six hours

}  // namespace

struct WebPagesUpdaterState {
    WebPagesConfig config;
    std::size_t pageCount = 0;
    HANDLE stop = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> cancelled{false};  // set when stopping, so late pages are not handed on

    std::vector<std::string> pages;  // empty = not saved yet
    std::vector<Meta> metas;

    // <cachePrefix><page name>[<suffix>], e.g. uniques-web-unique_armors.htm[.meta]
    std::wstring CacheName(std::size_t index, const wchar_t* suffix) const {
        std::wstring name = Wide(config.pageNames[index]);  // "frFR/gems.htm" is saved as "frFR_gems.htm"
        for (wchar_t& c : name)
            if (c == L'/' || c == L'\\') c = L'_';
        return config.cachePrefix + name + suffix;
    }

    void Log(const std::string& message) const {
        if (config.log && !cancelled) config.log(config.logPrefix + message);
    }

    bool Complete(const std::vector<std::string>& set) const {
        for (const std::string& page : set) if (page.empty()) return false;
        return true;
    }

    // Passes a set of pages on. False when the pages are not usable.
    bool Use(const std::vector<std::string>& set, const char* source) {
        if (cancelled) return false;
        std::string error;
        if (!config.use(set, error)) {
            Log(std::string("the ") + source + " pages were not used: " + error);
            return false;
        }
        return true;
    }

    void LoadSaved() {
        for (std::size_t i = 0; i < pageCount; ++i) {
            std::string metaText;
            if (!ReadTextFile(config.cacheDir + L"\\" + CacheName(i, L""), pages[i])) {
                pages[i].clear();
                continue;
            }
            if (ReadTextFile(config.cacheDir + L"\\" + CacheName(i, L".meta"), metaText)) {
                std::istringstream lines(metaText);
                std::getline(lines, metas[i].etag);
                std::getline(lines, metas[i].lastModified);
                for (std::string* value : {&metas[i].etag, &metas[i].lastModified}) {
                    while (!value->empty() && (value->back() == '\r' || value->back() == '\n')) value->pop_back();
                }
            }
        }
        if (Complete(pages)) {
            if (Use(pages, "saved")) {
                Log("using the copies saved from the website");
                return;
            }
        }
        // Saved copies that are no good, or an incomplete set: ask for every page afresh.
        pages.assign(pageCount, std::string());
        metas.assign(pageCount, Meta{});
    }

    void Run() {
        LoadSaved();
        for (;;) {
            if (cancelled) return;
            std::vector<std::string> next = pages;
            std::vector<Meta> nextMeta = metas;
            bool changed = false;
            bool failed = false;
            for (std::size_t i = 0; i < pageCount && !failed; ++i) {
                const FetchResult fetched =
                    Fetch(config.siteUrl + Wide(config.pageNames[i]), pages[i].empty() ? Meta{} : metas[i], stop, config.userAgent.c_str());
                if (fetched.kind == FetchResult::Kind::Page) {
                    next[i] = fetched.body;
                    nextMeta[i] = fetched.meta;
                    changed = true;
                } else if (fetched.kind == FetchResult::Kind::Failed) {
                    Log(std::string("could not check the website: ") + fetched.error);
                    failed = true;
                }
            }
            if (cancelled) return;
            if (!failed && changed && Complete(next)) {
                if (Use(next, "downloaded")) {
                    pages = next;
                    metas = nextMeta;
                    for (std::size_t i = 0; i < pageCount; ++i) {
                        if (!WriteFileAtomic(config.cacheDir + L"\\" + CacheName(i, L""), pages[i])) {
                            Log("could not save a copy of a page");
                            break;
                        }
                        WriteFileAtomic(config.cacheDir + L"\\" + CacheName(i, L".meta"), metas[i].etag + "\n" + metas[i].lastModified + "\n");
                    }
                    Log("updated from the website");
                }
            } else if (!failed && !changed) {
                Log("the website pages are unchanged");
            }
            if (cancelled || WaitForSingleObject(stop, kRecheckMs) == WAIT_OBJECT_0 || cancelled) return;
        }
    }
};

namespace {
DWORD WINAPI UpdaterThread(LPVOID parameter) {
    static_cast<WebPagesUpdaterState*>(parameter)->Run();
    return 0;
}
}  // namespace

WebPagesUpdater::WebPagesUpdater() : state_(std::make_unique<WebPagesUpdaterState>()) {}

WebPagesUpdater::~WebPagesUpdater() { Stop(); }

void WebPagesUpdater::Start(WebPagesConfig config) {
    if (!state_ || state_->thread != nullptr || !config.use || config.pageNames.empty()) return;
    const std::wstring optOut = config.cacheDir + L"\\" + config.optOutFile;
    state_->config = std::move(config);
    state_->pageCount = state_->config.pageNames.size();
    state_->pages.assign(state_->pageCount, std::string());
    state_->metas.assign(state_->pageCount, Meta{});
    // A file with this name next to the data turns the website check off.
    if (GetFileAttributesW(optOut.c_str()) != INVALID_FILE_ATTRIBUTES) {
        state_->Log("website updates are turned off (" + Narrow(state_->config.optOutFile) + ")");
        return;
    }
    state_->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state_->stop == nullptr) return;
    state_->thread = CreateThread(nullptr, 0, UpdaterThread, state_.get(), 0, nullptr);
}

void WebPagesUpdater::Stop() {
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
