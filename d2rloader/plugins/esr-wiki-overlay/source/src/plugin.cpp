// D2RLoader client plugin entry points for the ESR wiki overlay. Mirrors the
// D2RR Item Database plugin: load the data, start the overlay, and register the
// bindable action (unbound by default) and an `eswiki` console command.
#include <D2RLPlugin/api.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cube_updater.hpp"
#include "locale_live.hpp"
#include "web_pages_updater.hpp"
#include "debug_log.hpp"
#include "web_runes_live.hpp"
#include "web_bases_live.hpp"
#include "web_affixes_live.hpp"
#include "web_ascendancy_live.hpp"
#include "web_class_live.hpp"
#include "web_sets_live.hpp"
#include "web_endgame_live.hpp"
#include "web_special_live.hpp"
#include "web_skills_live.hpp"
#include "web_vessel_live.hpp"
#include "web_kill_live.hpp"
#include "web_mastery_live.hpp"
#include "web_words_live.hpp"
#include "web_uniques_live.hpp"
#include "overlay.hpp"
#include "wiki_model.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
using namespace D2RL;

constexpr D2RL::PluginInfo EsrOverlayPluginInfo {
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "esr-wiki-overlay",
    .name = "ESR Wiki Overlay",
    .version = "1.5.1",
    .author = "Sully",
    .description = "Read-only ESR wiki reference overlay. Open it with the eswiki command, or a key bound in the controls.",
    .flags = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext* pluginContext = nullptr;
const D2RL::InputService* input = nullptr;
D2RL::Input::ActionHandle openAction = D2RL::Input::InvalidHandle;
std::unique_ptr<esrwiki::Database> database;
std::unique_ptr<esrwiki::Overlay> overlay;
std::unique_ptr<esrwiki::Database> cubeDatabase;   // Horadric Cube recipes (cube-wiki-data.txt)
std::unique_ptr<esrwiki::Overlay> cubeOverlay;
bool cubeReady = false;                            // the cube window is running (guarded by webDataMutex)
std::string pendingCubeData;                       // the localized cube data built before the window was ready (webDataMutex)
esrwiki::CubeUpdater cubeUpdater;                  // keeps the cube recipes in step with the website
esrwiki::WebPagesUpdater webUniquesUpdater;        // keeps the Uniques page in step with the website
esrwiki::WebPagesUpdater webRunesUpdater;          // keeps the Runes page in step with the website
esrwiki::WebPagesUpdater webRunewordsUpdater;      // keeps the Runewords page in step with the website
esrwiki::WebPagesUpdater webGemwordsUpdater;       // keeps the Gemwords page in step with the website
esrwiki::WebPagesUpdater webBasesUpdater;          // keeps the Item Bases page in step with the website
esrwiki::WebPagesUpdater webAscendancyUpdater;      // keeps the Ascendancy page in step with the website
esrwiki::WebPagesUpdater webClassUpdater;            // keeps the Class Crafting page in step with the cube recipes website
esrwiki::WebPagesUpdater webSetsUpdater;            // keeps the Set Items page in step with the website
esrwiki::WebPagesUpdater webEndgameUpdater;         // keeps the Endgame Maps page in step with the website
esrwiki::WebPagesUpdater webSpecialUpdater;         // keeps the Special Craft page in step with the website
esrwiki::WebPagesUpdater webSkillsUpdater;          // keeps the Mercenary/O-Skill page in step with the website
esrwiki::WebPagesUpdater webVesselUpdater;          // keeps the Vessel of Souls page in step with the website
esrwiki::WebPagesUpdater webKillUpdater;            // keeps the Kill Ledger page in step with the website
esrwiki::WebPagesUpdater webMasteryUpdater;         // keeps the Weapon Mastery page in step with the website
esrwiki::WebPagesUpdater webAffixesUpdater;        // keeps the Item Affixes page in step with the website
esrwiki::WebPagesUpdater webLocalizedUpdater;      // keeps the localized wiki in step with the localized website pages
esrwiki::WebPagesUpdater webIasUpdater;            // keeps the IAS calculator data in step with the website
std::mutex webDataMutex;                           // the website updates build on liveWikiData one after another
std::string liveWikiData;                          // the shipped wiki data with every website page merged in so far
// Which panel was opened most recently; the wiki key reopens it.
enum class LastPanel { Main, Cube };
LastPanel lastPanel = LastPanel::Main;

// The cube recipes page on the ESR website, which the Horadric Cube panel follows.
// The ESR website, whose unique item pages, gems page and cube recipes page the Uniques and Runes pages follow.
constexpr wchar_t kSiteUrl[] = L"https://easternsunresurrected.com/";

constexpr wchar_t kCubePageUrl[] = L"https://easternsunresurrected.com/Eastern%20Sun%20Resurrected%20Cube%20Recipes.html";

// Item cards on the page with this id (0 when there is no such page).
std::size_t CountPageBlocks(const esrwiki::Database& data, const char* pageId) {
    std::size_t count = 0;
    for (const auto& page : data.pages)
        if (page.id == pageId)
            for (const auto& entry : page.entries) count += entry.blocks.size();
    return count;
}

// Merges one website update into the live wiki data. `build` makes the new data from the current data (which already
// holds the other website pages), so the Uniques and Runes updates do not undo each other. The new data is used only
// when it parses and its page `pageId` keeps at least 60% of the cards the shipped page had.
bool ApplyWebUpdate(const char* pageId, std::size_t shippedCards,
                    const std::function<bool(const std::string& current, std::string& next, std::string& error)>& build,
                    std::string& error) {
    std::lock_guard<std::mutex> lock(webDataMutex);
    std::string next;
    if (!build(liveWikiData, next, error)) return false;
    auto parsed = std::make_unique<esrwiki::Database>();
    std::string parseError;
    if (!esrwiki::ParseDatabase(next, *parsed, parseError)) {
        error = "website data rejected: " + parseError;
        return false;
    }
    if (CountPageBlocks(*parsed, pageId) * 10 < shippedCards * 6) {
        error = "the website pages have far fewer items than expected, ignored";
        return false;
    }
    if (overlay == nullptr) return false;
    overlay->ReplaceDatabase(std::move(parsed));
    liveWikiData = std::move(next);
    return true;
}

std::size_t CountRecipes(const esrwiki::Database& data) {
    std::size_t count = 0;
    for (const auto& page : data.pages)
        for (const auto& entry : page.entries)
            for (const auto& block : entry.blocks)
                for (const auto& line : block.lines)
                    if (line.kind == esrwiki::LineKind::Row) ++count;
    return count;
}

// Folder that holds this DLL. The wiki data sits in a folder next to it.
std::wstring PluginDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    const std::wstring full(path);
    const auto slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? full : full.substr(0, slash);
}

std::string ReadWholeFile(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open wiki-data.txt next to the plugin");
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

// The language set in the loader's menu (text_locale in d2rloader.toml; the config folder is next to the plugins folder that holds the DLL).
// Returns the locale code, or "" for English or when it is not set.
std::string LoaderLocale() {
    static const char* const kLocales[] = {"deDE", "esES", "frFR", "itIT", "koKR", "plPL", "ruRU", "zhCN",
                                           "zhTW", "esMX", "jaJP", "ptBR"};
    try {
        // The game's own loader config (the one the in-game settings menu writes), next to the game's executable.
        // The plugin's folder has its own copy for mods, so the plugin folder is only the fallback.
        std::string toml;
        try {
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            const std::wstring exePath(exe);
            const auto slash = exePath.find_last_of(L"\\/");
            const std::wstring gameDir = slash == std::wstring::npos ? exePath : exePath.substr(0, slash);
            toml = ReadWholeFile(gameDir + L"\\d2rloader\\config\\d2rloader.toml");
        } catch (const std::exception&) {
            toml = ReadWholeFile(PluginDirectory() + L"\\..\\config\\d2rloader.toml");
        }
        const std::size_t key = toml.find("\ntext_locale");
        if (key == std::string::npos) return "";
        const std::size_t open = toml.find('"', key);
        const std::size_t close = open == std::string::npos ? std::string::npos : toml.find('"', open + 1);
        if (close == std::string::npos) return "";
        const std::string code = toml.substr(open + 1, close - open - 1);
        for (const char* known : kLocales) {
            if (code == known) return code;
        }
        return "";
    } catch (const std::exception&) {
        return "";
    }
}

// Toggles the panel that was opened most recently (the main wiki unless cube recipes was last).
void toggleLastPanel() {
    if (lastPanel == LastPanel::Cube && cubeOverlay != nullptr) {
        cubeOverlay->Toggle();
    } else if (overlay != nullptr) {
        lastPanel = LastPanel::Main;
        overlay->Toggle();
    }
}

D2RL::Input::ActionResult __cdecl onOpenAction(const D2RL::PluginContext* plugin,
                                                const D2RL::Input::ActionEvent* event,
                                                void*) noexcept {
    if (plugin == nullptr || overlay == nullptr ||
        !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) ||
        event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }
    toggleLastPanel();
    return D2RL::Input::ActionResult::Handled;
}

// The key bound to the open action, read from the game's controls. Game actions do not fire while
// the overlay's search box has focus, so the overlay closes itself on the same key.
bool matchesOpenKey(unsigned int virtualKey) {
    if (pluginContext == nullptr || input == nullptr || openAction == D2RL::Input::InvalidHandle) return false;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    for (auto slot : {D2RL::Input::BindingSlot::Primary, D2RL::Input::BindingSlot::Secondary}) {
        D2RL::Input::Binding binding{};
        if (input->getBinding(pluginContext, openAction, slot, &binding) != D2RL::Input::Result::Success) continue;
        if (static_cast<unsigned int>(binding.key) != virtualKey) continue;
        switch (binding.modifier) {
            case D2RL::Input::Modifier::None: if (!shift && !control && !alt) return true; break;
            case D2RL::Input::Modifier::Shift: if (shift && !control && !alt) return true; break;
            case D2RL::Input::Modifier::Control: if (control && !shift && !alt) return true; break;
            case D2RL::Input::Modifier::Alt: if (alt && !shift && !control) return true; break;
        }
    }
    return false;
}

D2RL::ConsoleCommandResult toggleCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
    if (command == nullptr || command->plugin == nullptr || overlay == nullptr)
        return D2RL::ConsoleCommandResult::Failed;
    toggleLastPanel();
    return D2RL::ConsoleCommandResult::Handled;
}

// ---- Pause-menu button ------------------------------------------------------
// The "ESR Wiki" button on the bare pause menu. Its click sends
// PanelManager:OpenPanel:esr-wiki-overlay/OpenOverlay, which the listener below
// turns into Open(). The button is shown only while PauseLayoutGarden is open,
// and hidden while a vanilla pause sub-panel covers it.
const PanelService* panels = nullptr;
const SharedEventService* events = nullptr;
const ThreadService* threads = nullptr;
const ResourceService* resources = nullptr;
Panels::RegistrationHandle pauseButtonPanel = Panels::InvalidHandle;
Panels::RegistrationHandle openSelectorPanel = Panels::InvalidHandle;
SharedEvents::ListenerHandle messageHandle = SharedEvents::InvalidHandle;
bool pauseMenuOpen = false;
char obscuringPanel[64] = {};

constexpr const char* kPauseMenuPanel = "PauseLayoutGarden";
constexpr const char* kPauseButtonPanel = "esr-wiki-overlay/PauseButton";
constexpr const char* kOpenSelectorPanel = "esr-wiki-overlay/OpenOverlay";
// Vanilla pause sub-panels that cover the pause screen. The button hides while one is open.
constexpr const char* kObscuringPanels[] = {"SettingsPanel", "ChroniclePanel", "LootFilterOptionsPanel"};

constexpr char kPauseButtonLayout[] = R"JSON({
    "type": "Panel", "name": "esr-wiki-overlay/PauseButton",
    "fields": { "priority": 9500, "fitToParent": true },
    "children": [ {
        "type": "TableWidget", "name": "EsrWikiButtonTable",
        "fields": {
            "anchor": { "x": 0.5, "y": 0.5 },
            "rect": { "x": -329, "y": 395 },
            "columns": [ { "width": 658, "alignment": { "h": "center", "v": "center" } } ],
            "rowHeight": 140,
            "generateFocusLinks": true
        },
        "children": [ {
            "type": "TableRowWidget", "name": "Row ESR Wiki",
            "children": [ {
                "type": "ButtonWidget", "name": "OpenWikiButton",
                "fields": {
                    "filename": "PauseMenu\\PauseButton",
                    "hoveredFrame": 3,
                    "textString": "ESR Wiki",
                    "onClickMessage": "PanelManager:OpenPanel:esr-wiki-overlay/OpenOverlay",
                    "textColor": "$LightButtonTextColor",
                    "text/style": "$StyleFEButtonText",
                    "acceptsReturnKey": true,
                    "focusOnMouseOver": true,
                    "sound": "select"
                }
            } ]
        } ]
    } ]
})JSON";

constexpr char kSelectorLayout[] =
    R"JSON({"type":"Panel","name":"esr-wiki-overlay/OpenOverlay","fields":{"priority":1,"rect":{"x":0,"y":0,"width":1,"height":1}},"children":[]})JSON";

// Registers a layout from memory and a panel that uses it. Plugin layouts need
// their local id prefixed by the plugin id, so these names are esr-wiki-overlay/<id>.
bool RegisterPanelLayout(const PluginContext* plugin, const char* path, const char* json, const char* localId,
                         Panels::RegistrationHandle* handle) {
    const Resources::ResourceRegistration resource{Resources::ResourceRegistrationSize, 0, path, json, std::strlen(json)};
    Resources::RegistrationHandle resourceHandle = Resources::InvalidHandle;
    if (resources->registerResource(plugin, &resource, &resourceHandle) != Resources::Result::Success) return false;
    const Panels::PanelRegistration registration{Panels::PanelRegistrationSize, Panels::PanelFlags::None, localId};
    return panels->registerPanel(plugin, &registration, handle) == Panels::Result::Success;
}

// Panel open/close must run from a UI callback, so these are queued with runOnUiThread.
void __cdecl DoShowPauseButton(const PluginContext* plugin, void*) noexcept {
    if (plugin == nullptr || panels == nullptr || pauseButtonPanel == Panels::InvalidHandle) return;
    const Panels::Result result = panels->openPanel(plugin, pauseButtonPanel);
    if (result != Panels::Result::Success && result != Panels::Result::Conflict)
        plugin->LogError("ESR Wiki: could not show the pause button.");
}

void __cdecl DoHidePauseButton(const PluginContext* plugin, void*) noexcept {
    if (plugin == nullptr || panels == nullptr || pauseButtonPanel == Panels::InvalidHandle) return;
    const Panels::Result result = panels->closePanel(plugin, pauseButtonPanel);
    if (result != Panels::Result::Success && result != Panels::Result::Conflict)
        plugin->LogError("ESR Wiki: could not hide the pause button.");
}

// The selector is 1x1 and invisible, so close it right after it has been used.
void __cdecl DoCloseOpenSelector(const PluginContext* plugin, void*) noexcept {
    if (plugin == nullptr || panels == nullptr || openSelectorPanel == Panels::InvalidHandle) return;
    panels->closePanel(plugin, openSelectorPanel);
}

// ---- Horadric Cube recipes button -------------------------------------------
// The "Recipes" button on the Horadric Cube panel. It is shown while the native
// HoradricCubeLayout panel is open and opens the cube recipes overlay.
Panels::RegistrationHandle cubeButtonPanel = Panels::InvalidHandle;
Panels::RegistrationHandle openCubeSelectorPanel = Panels::InvalidHandle;
bool cubeOpen = false;

constexpr const char* kCubePanelName = "HoradricCubeLayout";
constexpr const char* kCubeButtonPanel = "esr-wiki-overlay/HoradricCubeButton";
constexpr const char* kOpenCubeSelectorPanel = "esr-wiki-overlay/OpenCubeOverlay";

constexpr char kCubeButtonLayout[] = R"JSON({
    "type": "Panel", "name": "esr-wiki-overlay/HoradricCubeButton",
    "fields": { "priority": 9500, "rect": "$LeftPanelRectS", "anchor": "$LeftPanelAnchor" },
    "children": [ {
        "type": "ButtonWidget", "name": "recipes",
        "fields": {
            "rect": { "x": 1502, "y": 1518, "scale": 1.6 },
            "filename": "panel\\hud_02\\questlog",
            "tooltipString": "Recipes",
            "hoveredFrame": 2,
            "onClickMessage": "PanelManager:OpenPanel:esr-wiki-overlay/OpenCubeOverlay",
            "frame": 5
        }
    } ]
})JSON";

constexpr char kCubeSelectorLayout[] =
    R"JSON({"type":"Panel","name":"esr-wiki-overlay/OpenCubeOverlay","fields":{"priority":1,"rect":{"x":0,"y":0,"width":1,"height":1}},"children":[]})JSON";

void __cdecl DoShowCubeButton(const PluginContext* plugin, void*) noexcept {
    if (plugin == nullptr || panels == nullptr || cubeButtonPanel == Panels::InvalidHandle) return;
    const Panels::Result result = panels->openPanel(plugin, cubeButtonPanel);
    if (result != Panels::Result::Success && result != Panels::Result::Conflict)
        plugin->LogError("ESR Wiki: could not show the cube recipes button.");
}

void __cdecl DoHideCubeButton(const PluginContext* plugin, void*) noexcept {
    if (plugin == nullptr || panels == nullptr || cubeButtonPanel == Panels::InvalidHandle) return;
    const Panels::Result result = panels->closePanel(plugin, cubeButtonPanel);
    if (result != Panels::Result::Success && result != Panels::Result::Conflict)
        plugin->LogError("ESR Wiki: could not hide the cube recipes button.");
}

void __cdecl DoCloseCubeSelector(const PluginContext* plugin, void*) noexcept {
    if (plugin == nullptr || panels == nullptr || openCubeSelectorPanel == Panels::InvalidHandle) return;
    panels->closePanel(plugin, openCubeSelectorPanel);
}

bool IsObscuringPanel(const char* text) noexcept {
    for (const char* name : kObscuringPanels) {
        if (std::strcmp(text, name) == 0) return true;
    }
    return false;
}

SharedEvents::UiMessageAction __cdecl OnUiMessage(const PluginContext* plugin, const SharedEvents::UiMessageEvent* event,
                                                  void*) noexcept {
    if (plugin == nullptr || event == nullptr || event->structSize < SharedEvents::UiMessageEventRequiredSize ||
        threads == nullptr) {
        return SharedEvents::UiMessageAction::Continue;
    }
    const char* target = event->target != nullptr ? event->target : "";
    const char* command = event->command != nullptr ? event->command : "";
    const char* text = event->text != nullptr ? event->text : "";
    if (std::strcmp(target, "PanelManager") != 0) return SharedEvents::UiMessageAction::Continue;

    if (std::strcmp(command, "OpenPanel") == 0 && std::strcmp(text, kOpenSelectorPanel) == 0) {
        if (threads->runOnUiThread(plugin, DoCloseOpenSelector, nullptr) != Threads::Result::Success)
            plugin->LogError("ESR Wiki: failed to queue closing the selector panel.");
        lastPanel = LastPanel::Main;
        if (overlay != nullptr) overlay->Open();
        return SharedEvents::UiMessageAction::Continue;
    }

    if (std::strcmp(command, "OpenPanel") == 0 && std::strcmp(text, kOpenCubeSelectorPanel) == 0) {
        if (threads->runOnUiThread(plugin, DoCloseCubeSelector, nullptr) != Threads::Result::Success)
            plugin->LogError("ESR Wiki: failed to queue closing the cube selector panel.");
        lastPanel = LastPanel::Cube;
        if (cubeOverlay != nullptr) cubeOverlay->Open();
        return SharedEvents::UiMessageAction::Continue;
    }

    if (std::strcmp(text, kCubePanelName) == 0) {
        if (std::strcmp(command, "OpenPanel") == 0) {
            cubeOpen = true;
            if (threads->runOnUiThread(plugin, DoShowCubeButton, nullptr) != Threads::Result::Success)
                plugin->LogError("ESR Wiki: failed to queue showing the cube recipes button.");
        } else if (std::strcmp(command, "ClosePanel") == 0) {
            cubeOpen = false;
            if (threads->runOnUiThread(plugin, DoHideCubeButton, nullptr) != Threads::Result::Success)
                plugin->LogError("ESR Wiki: failed to queue hiding the cube recipes button.");
        }
        return SharedEvents::UiMessageAction::Continue;
    }

    if (std::strcmp(text, kPauseMenuPanel) == 0) {
        if (std::strcmp(command, "OpenPanel") == 0) {
            pauseMenuOpen = true;
            obscuringPanel[0] = '\0';
            if (threads->runOnUiThread(plugin, DoShowPauseButton, nullptr) != Threads::Result::Success)
                plugin->LogError("ESR Wiki: failed to queue showing the pause button.");
        } else if (std::strcmp(command, "ClosePanel") == 0) {
            pauseMenuOpen = false;
            obscuringPanel[0] = '\0';
            if (threads->runOnUiThread(plugin, DoHidePauseButton, nullptr) != Threads::Result::Success)
                plugin->LogError("ESR Wiki: failed to queue hiding the pause button.");
        }
    } else if (pauseMenuOpen && std::strcmp(text, kPauseButtonPanel) != 0 && IsObscuringPanel(text)) {
        if (std::strcmp(command, "OpenPanel") == 0) {
            std::snprintf(obscuringPanel, sizeof(obscuringPanel), "%s", text);
            if (threads->runOnUiThread(plugin, DoHidePauseButton, nullptr) != Threads::Result::Success)
                plugin->LogError("ESR Wiki: failed to queue hiding the pause button.");
        } else if ((std::strcmp(command, "ClosePanel") == 0 || std::strcmp(command, "UnloadPanel") == 0) &&
                   obscuringPanel[0] != '\0' && std::strcmp(text, obscuringPanel) == 0) {
            obscuringPanel[0] = '\0';
            if (threads->runOnUiThread(plugin, DoShowPauseButton, nullptr) != Threads::Result::Success)
                plugin->LogError("ESR Wiki: failed to queue showing the pause button.");
        }
    }
    return SharedEvents::UiMessageAction::Continue;
}

// Optional feature: if the services are missing, log it and leave the overlay usable through the eswiki command.
// Registers the cube button and its selector. Needs the services SetupPauseButton queries.
bool SetupCubeButton(const PluginContext* plugin) {
    if (panels == nullptr || resources == nullptr || threads == nullptr) return false;
    if (!RegisterPanelLayout(plugin, "data/global/ui/layouts/esr-wiki-overlay/HoradricCubeButtonhd.json", kCubeButtonLayout,
                             "HoradricCubeButton", &cubeButtonPanel)) {
        plugin->LogError("ESR Wiki: failed to register the cube recipes button panel.");
        return false;
    }
    if (!RegisterPanelLayout(plugin, "data/global/ui/layouts/esr-wiki-overlay/OpenCubeOverlayhd.json", kCubeSelectorLayout,
                             "OpenCubeOverlay", &openCubeSelectorPanel)) {
        plugin->LogError("ESR Wiki: failed to register the cube selector panel.");
        return false;
    }
    return true;
}

// Optional feature: if the services are missing, log it and leave the overlay usable through the eswiki command.
bool SetupPauseButton(const PluginContext* plugin) {
    if (plugin->QueryService(&panels) != ServiceQueryResult::Success || !HasPanelServiceField(panels, PanelServiceRequiredSize)) {
        panels = nullptr;
        plugin->LogWarn("ESR Wiki pause button unavailable: PanelService missing.");
        return false;
    }
    if (plugin->QueryService(&events) != ServiceQueryResult::Success || !HasSharedEventServiceField(events, SharedEventServiceRequiredSize)) {
        events = nullptr;
        plugin->LogWarn("ESR Wiki pause button unavailable: SharedEventService missing.");
        return false;
    }
    if (plugin->QueryService(&threads) != ServiceQueryResult::Success || !HasThreadServiceField(threads, ThreadServiceRequiredSize)) {
        threads = nullptr;
        plugin->LogWarn("ESR Wiki pause button unavailable: ThreadService missing.");
        return false;
    }
    if (plugin->QueryService(&resources) != ServiceQueryResult::Success || !HasResourceServiceField(resources, ResourceServiceRequiredSize)) {
        resources = nullptr;
        plugin->LogWarn("ESR Wiki pause button unavailable: ResourceService missing.");
        return false;
    }
    if (!RegisterPanelLayout(plugin, "data/global/ui/layouts/esr-wiki-overlay/PauseButtonhd.json", kPauseButtonLayout,
                             "PauseButton", &pauseButtonPanel)) {
        plugin->LogError("ESR Wiki: failed to register the pause-button panel.");
        return false;
    }
    if (!RegisterPanelLayout(plugin, "data/global/ui/layouts/esr-wiki-overlay/OpenOverlayhd.json", kSelectorLayout,
                             "OpenOverlay", &openSelectorPanel)) {
        plugin->LogError("ESR Wiki: failed to register the open-overlay selector panel.");
        return false;
    }
    const SharedEvents::UiMessageListener listener{SharedEvents::UiMessageListenerSize, 0, 0, 0, OnUiMessage, nullptr};
    if (events->registerUiMessageListener(plugin, &listener, &messageHandle) != SharedEvents::Result::Success) {
        plugin->LogError("ESR Wiki: failed to register the UI message listener.");
        return false;
    }
    return true;
}

void Release() noexcept {
    cubeUpdater.Stop();
    webUniquesUpdater.Stop();
    webRunesUpdater.Stop();
    webRunewordsUpdater.Stop();
    webGemwordsUpdater.Stop();
    webBasesUpdater.Stop();
    webAffixesUpdater.Stop();
    webAscendancyUpdater.Stop();
    webClassUpdater.Stop();
    webSetsUpdater.Stop();
    webEndgameUpdater.Stop();
    webSpecialUpdater.Stop();
    webSkillsUpdater.Stop();
    webVesselUpdater.Stop();
    webKillUpdater.Stop();
    webLocalizedUpdater.Stop();
    webMasteryUpdater.Stop();
    webIasUpdater.Stop();
    {
        std::lock_guard<std::mutex> lock(webDataMutex);
        cubeReady = false;
        pendingCubeData.clear();
    }
    if (cubeOverlay) cubeOverlay->Stop();
    cubeOverlay.reset();
    cubeDatabase.reset();
    if (overlay) overlay->Stop();
    overlay.reset();
    database.reset();
}
}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &EsrOverlayPluginInfo; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* plugin) noexcept -> bool {
    try {
        if (!D2RL::HasContext(plugin)) return false;
        pluginContext = plugin;
        esrwiki::DebugSink() = [](const std::string& message) {
            if (pluginContext != nullptr) pluginContext->LogInfo(message.c_str());
        };
        plugin->LogInfo("ESR Wiki overlay load started");

        const std::wstring dataDir = PluginDirectory() + L"\\esr-wiki-overlay";
        database = std::make_unique<esrwiki::Database>();
        std::string error;
        // A translated main wiki (data\locales\<code>\wiki-data.txt) replaces the English one when the loader is set to
        // that language. Its pages are not updated from the website, which publishes English text.
        const std::string mainLocale = LoaderLocale();
        const std::wstring localizedWiki = mainLocale.empty() ? std::wstring()
            : dataDir + L"\\locales\\" + std::wstring(mainLocale.begin(), mainLocale.end()) + L"\\wiki-data.txt";
        const bool localizedMain = !localizedWiki.empty() && std::ifstream(localizedWiki).good();
        const std::string shippedWikiData = ReadWholeFile(localizedMain ? localizedWiki : dataDir + L"\\wiki-data.txt");
        const std::string webArtTable = ReadWholeFile(dataDir + L"\\web-unique-art.txt");
        const std::string webWordArtTable = ReadWholeFile(dataDir + L"\\web-word-art.txt");
        const std::string webBaseArtTable = ReadWholeFile(dataDir + L"\\web-base-art.txt");
        if (!esrwiki::ParseDatabase(shippedWikiData, *database, error)) {
            const std::string message = "ESR Wiki data rejected: " + error;
            plugin->LogError(message.c_str());
            database.reset();
            return false;
        }

        // The overlay's own labels in the loader's language (locales\<code>\ui-strings.txt); English stays as it is.
        const std::size_t uiStringCount = esrwiki::LoadUiStrings(mainLocale.empty() ? std::wstring()
            : dataDir + L"\\locales\\" + std::wstring(mainLocale.begin(), mainLocale.end()) + L"\\ui-strings.txt");
        esrwiki::ias::SetTranslator(&esrwiki::UiTextUtf8);
        esrwiki::DebugLog("loader language: " + (mainLocale.empty() ? std::string("none (English)") : mainLocale) + "; main wiki data: " +
                          (localizedMain ? "translated file" : "English file") + "; UI strings loaded: " + std::to_string(uiStringCount));
        overlay = std::make_unique<esrwiki::Overlay>();
        overlay->SetCloseKey(matchesOpenKey);
        if (!overlay->Start(*database, dataDir, esrwiki::UiText(L"ESR Wiki"), L"main",
                            {{esrwiki::UiText(L"Cube Recipes"), [] {
                                  lastPanel = LastPanel::Cube;
                                  if (cubeOverlay != nullptr) cubeOverlay->Open();
                              }, [] { return cubeOverlay != nullptr; }}})) {
            plugin->LogError("ESR Wiki overlay window could not be created");
            Release();
            return false;
        }

        const std::string loaded = "ESR Wiki overlay ready: " + std::to_string(database->pages.size()) +
                                   " pages, " + std::to_string(database->entryCount()) + " entries";
        plugin->LogInfo(loaded.c_str());
        for (const esrwiki::Page& page : database->pages) {
            esrwiki::DebugLog("page " + page.id + " '" + page.title + "': " + std::to_string(page.entries.size()) + " entries");
        }

        // The Uniques and Runes pages shipped with the plugin show at once; the website's current pages replace them
        // as soon as they have been read. Pages that give far fewer items than the shipped page are treated as
        // broken and ignored.
        if (localizedMain) esrwiki::DebugLog("translated wiki: the English page updaters are skipped, the localized website pages are used instead");
        if (localizedMain) try {
            // The pages the website supplies are rebuilt from the localized website: the English builders read the English
            // pages and the localized pages translate what they make. Everything else stays as the shipped file has it.
            const std::string englishShipped = ReadWholeFile(dataDir + L"\\wiki-data.txt");
            const std::string uiStrings = ReadWholeFile(dataDir + L"\\locales\\" + std::wstring(mainLocale.begin(), mainLocale.end()) + L"\\ui-strings.txt");
            const std::string shippedCube = ReadWholeFile(dataDir + L"\\locales\\" + std::wstring(mainLocale.begin(), mainLocale.end()) + L"\\cube-wiki-data.txt");
            {
                std::lock_guard<std::mutex> lock(webDataMutex);
                liveWikiData = shippedWikiData;
            }
            const auto log = [](const std::string& message) {
                if (pluginContext != nullptr) pluginContext->LogInfo(message.c_str());
            };
            if (englishShipped.empty()) {
                esrwiki::DebugLog("localized website pages: no English wiki-data.txt shipped, so they are not used");
            } else {
                auto shippedData = std::make_shared<esrwiki::Database>();
                std::string shippedError;
                esrwiki::ParseDatabase(shippedWikiData, *shippedData, shippedError);
                static const char* const kRebuiltPages[] = {"uniques", "runes", "runewords", "gemwords", "item-bases", "item-affixes",
                                                            "ascendancy", "class-crafting", "set-items", "endgame-maps", "corruption",
                                                            "merc-oskill", "vessel-of-souls", "kill-ledger", "weapon-mastery"};
                const std::wstring code(mainLocale.begin(), mainLocale.end());
                esrwiki::WebPagesConfig localized;
                localized.cacheDir = dataDir;
                localized.siteUrl = kSiteUrl;
                for (const char* name : esrwiki::kLocalizedPageNames) localized.pageNames.push_back(name);  // the English pages
                for (const char* name : esrwiki::kLocalizedPageNames) localized.pageNames.push_back(mainLocale + "/" + name);  // the same pages in the language
                localized.cachePrefix = L"locale-" + code + L"-web-";  // locale-frFR-web-frFR_gems.htm
                localized.optOutFile = esrwiki::kLiveUpdatesOffFile;
                localized.userAgent = L"ESR-Wiki-Overlay/1.5 (website " + code + L")";
                localized.logPrefix = "ESR Wiki website (" + mainLocale + "): ";
                localized.use = [englishShipped, uiStrings, shippedCube, shippedWikiData, shippedData, webArtTable, webWordArtTable, webBaseArtTable](
                                    const std::vector<std::string>& pages, std::string& error) {
                    esrwiki::LocalizedBuildInput build;
                    build.shippedEnglish = englishShipped;
                    build.shippedLocalized = shippedWikiData;
                    build.englishPages.assign(pages.begin(), pages.begin() + static_cast<std::ptrdiff_t>(esrwiki::kLocalizedPageCount));
                    build.localizedPages.assign(pages.begin() + static_cast<std::ptrdiff_t>(esrwiki::kLocalizedPageCount), pages.end());
                    build.uiStrings = uiStrings;
                    build.shippedCubeLocalized = shippedCube;
                    build.uniqueArtTable = webArtTable;
                    build.wordArtTable = webWordArtTable;
                    build.baseArtTable = webBaseArtTable;
                    std::string next, report, cubeNext;
                    if (!esrwiki::BuildLocalizedWikiData(build, next, report, error, &cubeNext)) return false;
                    auto parsed = std::make_unique<esrwiki::Database>();
                    std::string parseError;
                    if (!esrwiki::ParseDatabase(next, *parsed, parseError)) {
                        error = "website data rejected: " + parseError;
                        return false;
                    }
                    for (const char* id : kRebuiltPages)
                        if (CountPageBlocks(*parsed, id) * 10 < CountPageBlocks(*shippedData, id) * 6) {
                            error = std::string("the localized website pages give far fewer items for ") + id + " than expected, ignored";
                            return false;
                        }
                    // The Horadric Cube window's data: same checks (it parses, and keeps at least 60% of the shipped recipes).
                    std::unique_ptr<esrwiki::Database> cubeParsed;
                    if (!cubeNext.empty()) {
                        auto shippedCubeData = std::make_unique<esrwiki::Database>();
                        auto parsedCube = std::make_unique<esrwiki::Database>();
                        std::string cubeParseError, shippedParseError;
                        if (esrwiki::ParseDatabase(shippedCube, *shippedCubeData, shippedParseError) &&
                            esrwiki::ParseDatabase(cubeNext, *parsedCube, cubeParseError) &&
                            CountRecipes(*parsedCube) * 10 >= CountRecipes(*shippedCubeData) * 6) {
                            cubeParsed = std::move(parsedCube);
                        } else if (pluginContext != nullptr) {
                            pluginContext->LogError("ESR Wiki cube recipes: the localized website data was rejected, the shipped recipes stay");
                        }
                    }
                    std::lock_guard<std::mutex> lock(webDataMutex);
                    if (overlay == nullptr) return false;
                    overlay->ReplaceDatabase(std::move(parsed));
                    liveWikiData = std::move(next);
                    if (cubeParsed) {
                        if (cubeReady && cubeOverlay != nullptr) cubeOverlay->ReplaceDatabase(std::move(cubeParsed));
                        else pendingCubeData = std::move(cubeNext);  // the window is not up yet: it takes this when it starts
                    }
                    if (pluginContext != nullptr) {
                        std::size_t from = 0;
                        while (from < report.size()) {
                            const std::size_t to = report.find('\n', from);
                            const std::string line = report.substr(from, to == std::string::npos ? std::string::npos : to - from);
                            if (!line.empty()) pluginContext->LogInfo(("ESR Wiki website: " + line).c_str());
                            if (to == std::string::npos) break;
                            from = to + 1;
                        }
                    }
                    return true;
                };
                localized.log = log;
                webLocalizedUpdater.Start(std::move(localized));
            }
        } catch (...) {
            esrwiki::DebugLog("localized website pages: could not be started");
        }
        if (!localizedMain) try {
            {
                std::lock_guard<std::mutex> lock(webDataMutex);
                liveWikiData = shippedWikiData;
            }
            const std::size_t shippedUniques = CountPageBlocks(*database, "uniques");
            const std::size_t shippedRunes = CountPageBlocks(*database, "runes");
            const std::size_t shippedRunewords = CountPageBlocks(*database, "runewords");
            const std::size_t shippedGemwords = CountPageBlocks(*database, "gemwords");
            const std::size_t shippedBases = CountPageBlocks(*database, "item-bases");
            const std::size_t shippedAffixes = CountPageBlocks(*database, "item-affixes");
            const std::size_t shippedAscendancy = CountPageBlocks(*database, "ascendancy");
            const std::size_t shippedClass = CountPageBlocks(*database, "class-crafting");
            const std::size_t shippedSets = CountPageBlocks(*database, "set-items");
            const std::size_t shippedEndgame = CountPageBlocks(*database, "endgame-maps");
            const auto log = [](const std::string& message) {
                if (pluginContext != nullptr) pluginContext->LogInfo(message.c_str());
            };

            esrwiki::WebPagesConfig uniques;
            uniques.cacheDir = dataDir;
            uniques.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebUniquePageNames) uniques.pageNames.push_back(name);
            uniques.cachePrefix = L"uniques-web-";  // uniques-web-unique_armors.htm
            uniques.optOutFile = esrwiki::kLiveUpdatesOffFile;
            uniques.userAgent = L"ESR-Wiki-Overlay/1.4 (website uniques)";
            uniques.logPrefix = "ESR Wiki website uniques: ";
            uniques.use = [shippedUniques, webArtTable](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("uniques", shippedUniques,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebUniquesWikiData(current, pages, webArtTable, next, why);
                                      },
                                      error);
            };
            uniques.log = log;
            webUniquesUpdater.Start(std::move(uniques));

            esrwiki::WebPagesConfig runes;
            runes.cacheDir = dataDir;
            runes.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebRunesPageNames) runes.pageNames.push_back(name);  // gems page, cube recipes page
            runes.cachePrefix = L"runes-web-";  // runes-web-gems.htm
            runes.optOutFile = esrwiki::kLiveUpdatesOffFile;
            runes.userAgent = L"ESR-Wiki-Overlay/1.4 (website runes)";
            runes.logPrefix = "ESR Wiki website runes: ";
            runes.use = [shippedRunes](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("runes", shippedRunes,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebRunesWikiData(current, pages[0], pages[1], next, why);
                                      },
                                      error);
            };
            runes.log = log;
            webRunesUpdater.Start(std::move(runes));

            esrwiki::WebPagesConfig runewords;
            runewords.cacheDir = dataDir;
            runewords.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebRunewordsPageNames) runewords.pageNames.push_back(name);  // runewords page, gems page
            runewords.cachePrefix = L"runewords-web-";  // runewords-web-runewords.htm
            runewords.optOutFile = esrwiki::kLiveUpdatesOffFile;
            runewords.userAgent = L"ESR-Wiki-Overlay/1.4 (website runewords)";
            runewords.logPrefix = "ESR Wiki website runewords: ";
            runewords.use = [shippedRunewords, webWordArtTable](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("runewords", shippedRunewords,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebRunewordsWikiData(current, pages[0], pages[1], webWordArtTable, next, why);
                                      },
                                      error);
            };
            runewords.log = log;
            webRunewordsUpdater.Start(std::move(runewords));

            esrwiki::WebPagesConfig gemwords;
            gemwords.cacheDir = dataDir;
            gemwords.siteUrl = kSiteUrl;
            gemwords.pageNames.push_back(esrwiki::kWebGemwordsPageName);
            gemwords.cachePrefix = L"gemwords-web-";  // gemwords-web-gemwords.htm
            gemwords.optOutFile = esrwiki::kLiveUpdatesOffFile;
            gemwords.userAgent = L"ESR-Wiki-Overlay/1.4 (website gemwords)";
            gemwords.logPrefix = "ESR Wiki website gemwords: ";
            gemwords.use = [shippedGemwords, webWordArtTable](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("gemwords", shippedGemwords,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebGemwordsWikiData(current, pages[0], webWordArtTable, next, why);
                                      },
                                      error);
            };
            gemwords.log = log;
            webGemwordsUpdater.Start(std::move(gemwords));

            esrwiki::WebPagesConfig bases;
            bases.cacheDir = dataDir;
            bases.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebBasesPageNames) bases.pageNames.push_back(name);  // armors page, weapons page
            bases.cachePrefix = L"bases-web-";  // bases-web-armors.htm
            bases.optOutFile = esrwiki::kLiveUpdatesOffFile;
            bases.userAgent = L"ESR-Wiki-Overlay/1.4 (website item bases)";
            bases.logPrefix = "ESR Wiki website item bases: ";
            bases.use = [shippedBases, webBaseArtTable](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("item-bases", shippedBases,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebBasesWikiData(current, pages[0], pages[1], webBaseArtTable, next, why);
                                      },
                                      error);
            };
            bases.log = log;
            webBasesUpdater.Start(std::move(bases));

            esrwiki::WebPagesConfig affixes;
            affixes.cacheDir = dataDir;
            affixes.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebAffixesPageNames) affixes.pageNames.push_back(name);  // prefixes, suffixes, quality
            affixes.cachePrefix = L"affixes-web-";  // affixes-web-prefixes.htm
            affixes.optOutFile = esrwiki::kLiveUpdatesOffFile;
            affixes.userAgent = L"ESR-Wiki-Overlay/1.4 (website item affixes)";
            affixes.logPrefix = "ESR Wiki website item affixes: ";
            affixes.use = [shippedAffixes](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("item-affixes", shippedAffixes,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebAffixesWikiData(current, pages[0], pages[1], pages[2], next, why);
                                      },
                                      error);
            };
            affixes.log = log;
            webAffixesUpdater.Start(std::move(affixes));

            esrwiki::WebPagesConfig ascendancy;
            ascendancy.cacheDir = dataDir;
            ascendancy.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebAscendancyPageNames) ascendancy.pageNames.push_back(name);  // ascendancies.htm
            ascendancy.cachePrefix = L"ascendancy-web-";  // ascendancy-web-ascendancies.htm
            ascendancy.optOutFile = esrwiki::kLiveUpdatesOffFile;
            ascendancy.userAgent = L"ESR-Wiki-Overlay/1.4 (website ascendancies)";
            ascendancy.logPrefix = "ESR Wiki website ascendancies: ";
            ascendancy.use = [shippedAscendancy](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("ascendancy", shippedAscendancy,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebAscendancyWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            ascendancy.log = log;
            webAscendancyUpdater.Start(std::move(ascendancy));

            // Class Crafting comes from the cube recipes page, which the cube panel also reads.
            esrwiki::WebPagesConfig classCrafting;
            classCrafting.cacheDir = dataDir;
            classCrafting.siteUrl = kSiteUrl;
            classCrafting.pageNames.push_back("Eastern%20Sun%20Resurrected%20Cube%20Recipes.html");
            classCrafting.cachePrefix = L"class-crafting-web-";
            classCrafting.optOutFile = esrwiki::kLiveUpdatesOffFile;
            classCrafting.userAgent = L"ESR-Wiki-Overlay/1.4 (website cube recipes)";
            classCrafting.logPrefix = "ESR Wiki website class crafting: ";
            classCrafting.use = [shippedClass](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("class-crafting", shippedClass,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebClassCraftingWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            classCrafting.log = log;
            webClassUpdater.Start(std::move(classCrafting));

            esrwiki::WebPagesConfig sets;
            sets.cacheDir = dataDir;
            sets.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebSetsPageNames) sets.pageNames.push_back(name);  // sets.htm
            sets.cachePrefix = L"sets-web-";  // sets-web-sets.htm
            sets.optOutFile = esrwiki::kLiveUpdatesOffFile;
            sets.userAgent = L"ESR-Wiki-Overlay/1.4 (website sets)";
            sets.logPrefix = "ESR Wiki website sets: ";
            sets.use = [shippedSets](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("set-items", shippedSets,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebSetsWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            sets.log = log;
            webSetsUpdater.Start(std::move(sets));

            esrwiki::WebPagesConfig endgame;
            endgame.cacheDir = dataDir;
            endgame.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebEndgamePageNames) endgame.pageNames.push_back(name);  // endgame_maps.htm
            endgame.cachePrefix = L"endgame-web-";  // endgame-web-endgame_maps.htm
            endgame.optOutFile = esrwiki::kLiveUpdatesOffFile;
            endgame.userAgent = L"ESR-Wiki-Overlay/1.4 (website endgame maps)";
            endgame.logPrefix = "ESR Wiki website endgame maps: ";
            endgame.use = [shippedEndgame](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("endgame-maps", shippedEndgame,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebEndgameWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            endgame.log = log;
            webEndgameUpdater.Start(std::move(endgame));

            // The Special Craft page: the corruption, anointment, sanctification and standard of heroes outcomes.
            const std::size_t shippedSpecial = CountPageBlocks(*database, "corruption");
            esrwiki::WebPagesConfig special;
            special.cacheDir = dataDir;
            special.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebSpecialPageNames) special.pageNames.push_back(name);
            special.cachePrefix = L"special-web-";  // special-web-special_crafting_outcomes.htm
            special.optOutFile = esrwiki::kLiveUpdatesOffFile;
            special.userAgent = L"ESR-Wiki-Overlay/1.4 (website special crafting)";
            special.logPrefix = "ESR Wiki website special crafting: ";
            special.use = [shippedSpecial](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("corruption", shippedSpecial,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebSpecialWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            special.log = log;
            webSpecialUpdater.Start(std::move(special));

            // The Mercenary/O-Skill page: the mercenary and oskill tables from the website.
            const std::size_t shippedSkills = CountPageBlocks(*database, "merc-oskill");
            esrwiki::WebPagesConfig skills;
            skills.cacheDir = dataDir;
            skills.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebSkillsPageNames) skills.pageNames.push_back(name);
            skills.cachePrefix = L"skills-web-";  // skills-web-skill_information.htm
            skills.optOutFile = esrwiki::kLiveUpdatesOffFile;
            skills.userAgent = L"ESR-Wiki-Overlay/1.4 (website skill information)";
            skills.logPrefix = "ESR Wiki website skill information: ";
            skills.use = [shippedSkills](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("merc-oskill", shippedSkills,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebSkillsWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            skills.log = log;
            webSkillsUpdater.Start(std::move(skills));

            // The Vessel of Souls page: the overview, the four tiers and the completion from the website.
            const std::size_t shippedVessel = CountPageBlocks(*database, "vessel-of-souls");
            esrwiki::WebPagesConfig vessel;
            vessel.cacheDir = dataDir;
            vessel.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebVesselPageNames) vessel.pageNames.push_back(name);
            vessel.cachePrefix = L"vessel-web-";  // vessel-web-vessel_of_souls.htm
            vessel.optOutFile = esrwiki::kLiveUpdatesOffFile;
            vessel.userAgent = L"ESR-Wiki-Overlay/1.4 (website vessel of souls)";
            vessel.logPrefix = "ESR Wiki website vessel of souls: ";
            vessel.use = [shippedVessel](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("vessel-of-souls", shippedVessel,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebVesselWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            vessel.log = log;
            webVesselUpdater.Start(std::move(vessel));

            // The Kill Ledger page: the kill bonuses and boss bonuses from the website.
            const std::size_t shippedKill = CountPageBlocks(*database, "kill-ledger");
            esrwiki::WebPagesConfig kill;
            kill.cacheDir = dataDir;
            kill.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebKillPageNames) kill.pageNames.push_back(name);
            kill.cachePrefix = L"kill-web-";  // kill-web-kill_ledger.htm
            kill.optOutFile = esrwiki::kLiveUpdatesOffFile;
            kill.userAgent = L"ESR-Wiki-Overlay/1.4 (website kill ledger)";
            kill.logPrefix = "ESR Wiki website kill ledger: ";
            kill.use = [shippedKill](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("kill-ledger", shippedKill,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebKillWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            kill.log = log;
            webKillUpdater.Start(std::move(kill));

            // The Weapon Mastery page: the general recipes and the specialization variants from the website.
            const std::size_t shippedMastery = CountPageBlocks(*database, "weapon-mastery");
            esrwiki::WebPagesConfig mastery;
            mastery.cacheDir = dataDir;
            mastery.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::kWebMasteryPageNames) mastery.pageNames.push_back(name);
            mastery.cachePrefix = L"mastery-web-";  // mastery-web-weapon_mastery.html
            mastery.optOutFile = esrwiki::kLiveUpdatesOffFile;
            mastery.userAgent = L"ESR-Wiki-Overlay/1.4 (website weapon mastery)";
            mastery.logPrefix = "ESR Wiki website weapon mastery: ";
            mastery.use = [shippedMastery](const std::vector<std::string>& pages, std::string& error) {
                return ApplyWebUpdate("weapon-mastery", shippedMastery,
                                      [&](const std::string& current, std::string& next, std::string& why) {
                                          return esrwiki::BuildWebMasteryWikiData(current, pages[0], next, why);
                                      },
                                      error);
            };
            mastery.log = log;
            webMasteryUpdater.Start(std::move(mastery));

        } catch (const std::exception& error) {
            const std::string message = std::string("ESR Wiki website updates unavailable: ") + error.what();
            plugin->LogError(message.c_str());
        }

        // The IAS calculator has no page text of its own, so it loads in every language (a translated wiki skips
        // only the updaters that would put English pages back).
        try {
            const auto log = [](const std::string& message) {
                if (pluginContext != nullptr) pluginContext->LogInfo(message.c_str());
            };
            // The IAS calculator: the data that shipped, then whatever the website has.
            {
                auto shipped = std::make_shared<esrwiki::ias::Data>();
                std::string why;
                if (esrwiki::ias::Parse(ReadWholeFile(dataDir + L"\\ias-calculator.html"),
                                        ReadWholeFile(dataDir + L"\\ias-calculator-data.js"), *shipped, why)) {
                    overlay->SetCalculatorData(shipped);
                    esrwiki::DebugLog("IAS calculator: shipped data loaded");
                } else {
                    log("ESR Wiki IAS calculator: the shipped data cannot be used: " + why);
                }
            }
            esrwiki::WebPagesConfig iasConfig;
            iasConfig.cacheDir = dataDir;
            iasConfig.siteUrl = kSiteUrl;
            for (const char* name : esrwiki::ias::kPageNames) iasConfig.pageNames.push_back(name);  // the page, its data file
            iasConfig.cachePrefix = L"ias-web-";  // ias-web-ias-calculator.html
            iasConfig.optOutFile = esrwiki::kLiveUpdatesOffFile;
            iasConfig.userAgent = L"ESR-Wiki-Overlay/1.4 (website IAS calculator)";
            iasConfig.logPrefix = "ESR Wiki website IAS calculator: ";
            iasConfig.use = [](const std::vector<std::string>& pages, std::string& error) {
                auto fresh = std::make_shared<esrwiki::ias::Data>();
                if (!esrwiki::ias::Parse(pages[0], pages[1], *fresh, error)) return false;
                if (overlay != nullptr) overlay->SetCalculatorData(fresh);
                return true;
            };
            iasConfig.log = log;
            webIasUpdater.Start(std::move(iasConfig));
        } catch (const std::exception& error) {
            const std::string message = std::string("ESR Wiki website updates unavailable: ") + error.what();
            plugin->LogError(message.c_str());
        }

        // Horadric Cube recipes: a second overlay with its own data. Optional, so failures only log.
        try {
            cubeDatabase = std::make_unique<esrwiki::Database>();
            std::string cubeError;
            // A translated cube file (data\locales\<code>) replaces the English one when the loader is set to that
            // language. Those recipes are the shipped fallback; the localized website build (above) replaces them live.
            const std::string locale = LoaderLocale();
            const std::wstring localized = locale.empty() ? std::wstring()
                : dataDir + L"\\locales\\" + std::wstring(locale.begin(), locale.end()) + L"\\cube-wiki-data.txt";
            const bool localizedCube = !localized.empty() && std::ifstream(localized).good();
            esrwiki::DebugLog(std::string("cube recipes: ") + (localizedCube ? "translated file (replaced live from the localized website)" : "English file"));
            if (esrwiki::ParseDatabase(ReadWholeFile(localizedCube ? localized : dataDir + L"\\cube-wiki-data.txt"),
                                       *cubeDatabase, cubeError)) {
                cubeOverlay = std::make_unique<esrwiki::Overlay>();
                cubeOverlay->SetCloseKey(matchesOpenKey);
                const bool started = cubeOverlay->Start(
                    *cubeDatabase, dataDir, esrwiki::UiText(L"Horadric Cube Recipes"), L"cube",
                    {{esrwiki::UiText(L"Main Wiki"), [] {
                          lastPanel = LastPanel::Main;
                          if (overlay != nullptr) overlay->Open();
                      }, [] { return overlay != nullptr; }}});
                if (!started) {
                    plugin->LogError("ESR Wiki cube recipes window could not be created");
                    cubeOverlay.reset();
                } else {
                    // The recipes shipped with the plugin show at once; the website's current page replaces
                    // them as soon as it has been read. A page with far fewer recipes than the shipped data
                    // is treated as a broken page and ignored.
                    const std::size_t shipped = CountRecipes(*cubeDatabase);
                    {   // a localized build that finished before this window existed is applied now
                        std::lock_guard<std::mutex> lock(webDataMutex);
                        cubeReady = true;
                        if (!pendingCubeData.empty()) {
                            auto pendingParsed = std::make_unique<esrwiki::Database>();
                            std::string pendingError;
                            if (esrwiki::ParseDatabase(pendingCubeData, *pendingParsed, pendingError))
                                cubeOverlay->ReplaceDatabase(std::move(pendingParsed));
                            pendingCubeData.clear();
                        }
                    }
                    if (!localizedCube) cubeUpdater.Start(
                        dataDir, kCubePageUrl,
                        [shipped](const std::string& wikiData) {
                            auto next = std::make_unique<esrwiki::Database>();
                            std::string parseError;
                            if (!esrwiki::ParseDatabase(wikiData, *next, parseError)) {
                                if (pluginContext != nullptr)
                                    pluginContext->LogError(("ESR Wiki cube recipes: website data rejected: " + parseError).c_str());
                                return;
                            }
                            if (CountRecipes(*next) * 10 < shipped * 6) {
                                if (pluginContext != nullptr)
                                    pluginContext->LogError("ESR Wiki cube recipes: website page has far fewer recipes than expected, ignored");
                                return;
                            }
                            if (cubeOverlay != nullptr) cubeOverlay->ReplaceDatabase(std::move(next));
                        },
                        [](const std::string& message) {
                            if (pluginContext != nullptr) pluginContext->LogInfo(message.c_str());
                        });
                }
            } else {
                const std::string message = "ESR Wiki cube recipes data rejected: " + cubeError;
                plugin->LogError(message.c_str());
                cubeDatabase.reset();
            }
        } catch (const std::exception& error) {
            const std::string message = std::string("ESR Wiki cube recipes unavailable: ") + error.what();
            plugin->LogError(message.c_str());
            cubeOverlay.reset();
            cubeDatabase.reset();
        }

        if (!plugin->RegisterConsoleCommand("eswiki", toggleCommand, "Open or close the ESR Wiki overlay.")) {
            plugin->LogError("ESR Wiki console command registration failed");
            Release();
            return false;
        }

        if (plugin->QueryService(&input) == D2RL::ServiceQueryResult::Success &&
            D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
            const D2RL::Input::ActionRegistration action{
                .structSize = D2RL::Input::ActionRegistrationSize,
                .logicalId = "open-esr-wiki",
                .displayName = "Open ESR Wiki",
                .category = "ESR Wiki",
                .defaultPrimary = {.key = D2RL::Input::Key::None},  // unbound by default; set a key in the game's controls
                .defaultSecondary = {.key = D2RL::Input::Key::None},
                .callback = onOpenAction,
            };
            if (input->registerAction(plugin, &action, &openAction) != D2RL::Input::Result::Success)
                plugin->LogWarn("ESR Wiki action was not registered; use the eswiki console command");
        } else {
            plugin->LogWarn("ESR Wiki input service unavailable; use the eswiki console command");
        }

        if (!SetupPauseButton(plugin)) plugin->LogWarn("ESR Wiki: use the eswiki command to open the wiki.");
        if (!SetupCubeButton(plugin)) plugin->LogWarn("ESR Wiki: Horadric Cube recipes button unavailable.");
        plugin->LogInfo("ESR Wiki overlay load complete");
        return true;
    } catch (const std::exception& error) {
        if (plugin != nullptr) {
            const std::string message = std::string("ESR Wiki overlay initialization failure: ") + error.what();
            plugin->LogError(message.c_str());
        }
        Release();
        return false;
    } catch (...) {
        if (plugin != nullptr) plugin->LogError("ESR Wiki overlay initialization failure: unknown exception");
        Release();
        return false;
    }
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (events != nullptr && messageHandle != SharedEvents::InvalidHandle && pluginContext != nullptr) {
        events->unregisterUiMessageListener(pluginContext, messageHandle);
        messageHandle = SharedEvents::InvalidHandle;
    }
    Release();
    if (pluginContext != nullptr) pluginContext->LogInfo("ESR Wiki overlay unloaded");
    pluginContext = nullptr;
}
