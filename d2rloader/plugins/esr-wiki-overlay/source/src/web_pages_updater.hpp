// Keeps pages of the wiki in step with pages of the ESR website (the unique item pages, the gems page).
//
// A background thread reads the pages it saved last time, then asks the site for newer ones (conditional
// requests, so nothing is downloaded when a page is unchanged) and again every few hours while the game runs.
// Whenever the set of pages is usable, `use` is called with them. A failed download or a page that cannot be
// read leaves the data as it was.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace esrwiki {

// One switch for every website update (the wiki pages, the IAS calculator, the cube recipes and the localized pages): a file
// with this name in the plugin's data folder turns them all off. The plugin then shows what it shipped with.
inline constexpr wchar_t kLiveUpdatesOffFile[] = L"live-updates-disabled.txt";

struct WebPagesUpdaterState;

struct WebPagesConfig {
    std::wstring cacheDir;                // holds the saved copies of the pages
    std::wstring siteUrl;                 // ends with a slash
    std::vector<std::string> pageNames;   // the website pages, e.g. "gems.htm"
    std::wstring cachePrefix;             // saved copies are <cachePrefix><page name> and <...>.meta
    std::wstring optOutFile;              // always kLiveUpdatesOffFile: a file with this name in cacheDir turns the website check off
    std::wstring userAgent;
    std::string logPrefix;                // starts every log line, e.g. "ESR Wiki website runes: "
    // Builds the new wiki data from the complete set of pages (in pageNames order) and applies it. Returns
    // false, with the reason in `error`, when the pages are not usable. Runs on the updater's thread.
    std::function<bool(const std::vector<std::string>& pages, std::string& error)> use;
    std::function<void(const std::string& message)> log;
};

class WebPagesUpdater {
public:
    WebPagesUpdater();
    ~WebPagesUpdater();
    WebPagesUpdater(const WebPagesUpdater&) = delete;
    WebPagesUpdater& operator=(const WebPagesUpdater&) = delete;

    void Start(WebPagesConfig config);
    void Stop();

private:
    std::unique_ptr<WebPagesUpdaterState> state_;
};

}  // namespace esrwiki
