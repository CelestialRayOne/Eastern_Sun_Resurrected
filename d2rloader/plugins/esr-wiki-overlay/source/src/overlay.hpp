// Win32 overlay that browses the ESR wiki data. Hosted on its own UI thread,
// like the D2RR Item Database overlay; the rest of the plugin never touches
// window handles.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ias_calc.hpp"
#include "wiki_model.hpp"

namespace esrwiki {

struct OverlayState;  // window and UI state, defined in overlay.cpp

// The overlay's own labels in the loader's language, from locales\<code>\ui-strings.txt (one "English<TAB>text" line
// each). Text with no translation is shown in English. UiFormat replaces {0}, {1}, ... with the arguments.
std::size_t LoadUiStrings(const std::wstring& path);  // returns how many strings it read
std::wstring UiText(const std::wstring& english);
std::wstring UiFormat(const std::wstring& english, const std::vector<std::wstring>& args);
// Same lookup for UTF-8 text (the calculator's); returns an empty string when there is no translation.
std::string UiTextUtf8(const std::string& english);

// A button in a panel's header that opens another panel. `onSwitch` opens that panel (and hands the screen to it).
struct SwitchButton {
    std::wstring label;
    std::function<void()> onSwitch;
    // Optional: when it returns false the button does nothing, so the panel stays open (its target is not running).
    std::function<bool()> available;
    // The panel this button is on: drawn as the active one, and it does nothing when clicked.
    bool current = false;
};

class Overlay {
public:
    Overlay();
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // `database` and the image folder must outlive the overlay. `dataDir` is the
    // folder that holds images\<key>.png. `title` is shown in the header, and
    // `instanceId` keeps this window's class names unique when two overlays run.
    // Each of `switches` is a button in the header that opens another panel. Returns false if the window cannot start.
    bool Start(const Database& database, const std::wstring& dataDir, const std::wstring& title,
               const std::wstring& instanceId, const std::vector<SwitchButton>& switches = {});
    void Stop();
    void Toggle();  // show or hide; safe to call from any thread
    void Open();    // show without toggling; safe to call from any thread
    // Replaces the data shown with `next`, which the overlay then owns. Safe to call from any thread; the
    // switch happens on the overlay's own thread. Returns false if the overlay is not running or `next` is empty.
    bool ReplaceDatabase(std::unique_ptr<Database> next);
    // The data of the IAS calculator page (the website's Frame Table). Safe to call from any thread; the page shows
    // the newest data at its next paint. Without data the page says so.
    void SetCalculatorData(std::shared_ptr<const ias::Data> data);
    // Key that closes the overlay while its own window has keyboard focus (the game's actions
    // do not fire then). Call before Start.
    void SetCloseKey(std::function<bool(unsigned int virtualKey)> isCloseKey);

private:
    std::unique_ptr<OverlayState> impl_;
};

}  // namespace esrwiki
