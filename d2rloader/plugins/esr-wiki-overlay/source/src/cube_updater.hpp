// Keeps the Horadric Cube recipes in step with the cube recipes page on the ESR website.
//
// A background thread reads the last page it saved, then asks the site for a newer one (a
// conditional request, so nothing is downloaded when the page is unchanged) and again every few
// hours while the game runs. Each usable page is turned into wiki data by BuildCubeWikiData and
// handed to `apply`. A failed download or a page that cannot be read leaves the data as it was.
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace esrwiki {

struct CubeUpdaterState;

class CubeUpdater {
public:
    CubeUpdater();
    ~CubeUpdater();
    CubeUpdater(const CubeUpdater&) = delete;
    CubeUpdater& operator=(const CubeUpdater&) = delete;

    // `cacheDir` holds the saved copy of the page. `apply` runs on the updater's thread with
    // the new wiki data. `log` reports what happened, one line at a time.
    void Start(const std::wstring& cacheDir, const std::wstring& url, std::function<void(const std::string& wikiData)> apply,
               std::function<void(const std::string& message)> log);
    void Stop();

private:
    std::unique_ptr<CubeUpdaterState> state_;
};

}  // namespace esrwiki
