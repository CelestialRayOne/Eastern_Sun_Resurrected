# ESR Wiki Overlay 1.5.1 (source)

Source for the ESR Wiki overlay plugin for Diablo II Resurrected (Eastern Sun Resurrected),
loaded by D2RLoader. It contains the C++ overlay, the Python tools that generate its data,
and the generated data files.

Author: Sully

AI disclosure: see [AI_DISCLOSURE.md](AI_DISCLOSURE.md).

Credits: [CREDITS.md](CREDITS.md). Third-party licenses: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Layout

```
CMakeLists.txt        build (Visual Studio 2022, x64)
deploy.cmake          copies the built files into a mod's d2rloader folder
pages.json            page list and tab order; add categories here
src/
  wiki_model.*        portable parser, search, paging and color codes (no Win32)
  overlay.*           Win32 window and GDI+ art rendering
  cursor_art.hpp      generated cursor image used by overlay.cpp
  plugin.cpp          D2RLoader entry points, the eswiki command and bindable action
tests/
  wiki_model_test.cpp portable model tests
  cube_live_test.cpp  checks the C++ cube page reader against tests/cube_live/ (a saved copy of the page and the
                      output the Python tools give for it)
  test_extract.py      extractor tests (unittest)
tools/
  extract_wiki.py      builds data/wiki-data.txt from the mod's layout JSON and Excel tables
  build_cube.py        builds data/cube-data.txt from the cube recipes web page (saved HTML, or the live page)
  build_cube_wiki.py   builds data/cube-wiki-data.txt from cube-data.txt
  web_runes.py         builds the Runes page from the website gems page in data/website-runes/gems.htm (the C++ twin is src/web_runes_live.cpp)
  web_uniques.py       builds the Uniques page from the website pages in data/website-uniques/ (the C++ twin is src/web_uniques_live.cpp)
  runeword_gen.py, runeword_desc.py   stat and description lines for runeword and gemword cards (from runes.txt, gems.txt)
  make_cursor.py       builds src/cursor_art.hpp (the fallback gauntlet cursor, from ohand; the overlay uses the game's own cursor when it can)
  make_runeword_art.py one-time export of the runeword card art to data/runeword-art.json
  sprites_to_png.py    converts D2R .sprite files to PNG art
  excel_items.py, wiki_layout.py, _probe_util.py   helpers used by the tools
data/
  wiki-data.txt, cube-wiki-data.txt, cube-data.txt   generated data files
  runeword-art.json    card art, titles and order for the Runewords page (the art varies within each item
                       category so the cards do not all look alike); the extractor reads it
```

## Inputs the tools need

The generators read files from the Eastern Sun Resurrected mod and the base game, which
are not included here. Point the tools at your own copies:

- the mod's layout JSON files (`*hd.json`) and string table
- the mod's Excel tables (uniqueitems, setitems, runes, gems, gamble, armor, weapons, misc, itemtypes, cubemain)
- the mod's `hd/items/uniques.json` for unique artwork
- the mod's and the base game's `hd/global/ui` sprite folders for art

Example:

```powershell
python tools/extract_wiki.py --excel "<mod>\data\global\excel" `
  --sprite-root "<mod>\Eastern_Sun_Resurrected.mpq\data\hd\global\ui" `
  --base-ui-root "<base game>\data\data\hd\global\ui" `
  --uniques-json "<mod>\Eastern_Sun_Resurrected.mpq\data\hd\items\uniques.json"
python tools/sprites_to_png.py --source "<sprite folder>"
```

Run the generators from the `tools` folder. The image output folder (`data/images`) is not
part of this source package; `sprites_to_png.py` creates it.

## Build (Windows, Visual Studio 2022 C++ tools)

The build needs the D2RLoader Plugin SDK headers. Point CMake at them with
`-DESR_PLUGIN_SDK_INCLUDE=<path>`.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DESR_PLUGIN_SDK_INCLUDE=<path>
cmake --build build --config Release
```

Then copy `d2rl-esr-wiki-overlay.dll`, `wiki-data.txt`, `cube-wiki-data.txt` and `images/`
into your mod's `d2rloader/plugins` folder (see the standalone package for the layout).

## Tests

```powershell
python -m unittest discover -s tests -p "test_*.py"
```

`test_extract.py` needs the generator inputs above, so run it where they are available.

## Adding a category

1. Add the layout JSON to the inputs.
2. Add an entry to `pages.json`. `"mode": "guide"` makes each heading an entry;
   `"mode": "cards"` makes each matching node a card.
3. Run the generators, then rebuild. No C++ change is needed for a new page.

## Where card data comes from

Runeword, unique and set cards take their orange description lines and stat lines from the mod's
own Excel tables and string files, not from the website. The Runewords page is built from
`runes.txt`; gemwords are kept on the Gemwords page only. Unique cards show a "Gamble Item" line
(the Normal base item and the unique's item level) when `gamble.txt` lists its base; Coupon,
Mythical and non-spawnable uniques never get it.

The Hireling category of the Set Items page is built from setitems.txt and sets.txt (the rows whose set
ends in -N, -NM or -H), laid out like the Warlock sets: a header for each set and difficulty, its pieces,
then its set bonus card, in Normal, Nightmare, Hell order. It can be filtered by difficulty.

## Cube recipes from the website
`src/cube_live.cpp` reads the cube recipes page (HTML) and writes the same data as `tools/build_cube.py` plus
`tools/build_cube_wiki.py`; `src/cube_updater.cpp` downloads it (WinHTTP, conditional request, cached next to the data,
rechecked every six hours) and the plugin swaps the result into the Horadric Cube window. The grouping rules (which site
tabs make which page, class crafting left out of Legacy Craft) exist in both places; if the page changes, update both and
refresh `tests/cube_live/` (save the page there as cube-recipes-page.html and rerun the Python tools to rewrite
cube-wiki-data.expected.txt). `scripts/publish-overlay.ps1` rebuilds the shipped cube data from the live page
(skip with -SkipCubeRefresh). A file named `cube-live-disabled.txt` in the data folder turns the in-game check off.

## Uniques from the website
The Uniques page is built from unique_others/weapons/armors/mythicals.htm. `tools/web_uniques.py` does it at publish time
(`scripts/publish-overlay.ps1` downloads the pages into data/website-uniques/; skip with -SkipWebsiteUniques), and
`src/web_uniques_live.cpp` with `src/web_pages_updater.cpp` redo it in the game (rechecked every six hours, cached next to
the data). Art, tier and weapon family come from data/web-unique-art.txt. A file named `uniques-live-disabled.txt` in the
data folder turns the in-game check off. Both parsers must agree; `web_uniques_live_test` checks that.

## Runes from the website
The Runes page is built from `gems.htm` on the website (the gems on that page are not shown); the recipe table in its
"How to Acquire Elemental Runes" note comes from the Cube Recipes page. `tools/web_runes.py`
does it at publish time (`scripts/publish-overlay.ps1` downloads the page into data/website-runes/; skip with
-SkipWebsiteRunes), and `src/web_runes_live.cpp` with `src/web_pages_updater.cpp` redo it in the game (both pages are downloaded) the same way as
the Uniques page (when the game starts and every six hours, cached next to the data). A file named
`runes-live-disabled.txt` in the data folder turns the in-game check off. Runes are grouped by the colour of their name on the
page (none = LoD, a #rrggbb colour = Elemental, a named colour = Kanji); equal columns are shown once under the website's own
labels. The shared HTML reading code is `src/web_html.hpp`. Both builders must agree; `web_runes_live_test` checks that.

## Runewords and gemwords from the website
The Runewords page is built from `runewords.htm` (levels from `gems.htm`) and the Gemwords page from `gemwords.htm`.
`tools/web_words.py` does it at publish time (`scripts/publish-overlay.ps1` downloads the pages into data/website-words/;
skip with -SkipWebsiteWords), and `src/web_words_live.cpp` with `src/web_pages_updater.cpp` redo it in the game (two more
updaters, cached next to the data). The pictures the website does not give come from data/web-word-art.txt, which the
extractor writes from the mod-built pages. Files named `runewords-live-disabled.txt` and `gemwords-live-disabled.txt`
turn the in-game checks off. Both builders must agree; `web_words_live_test` checks that.

## Item bases from the website
The Item Bases page is built from `armors.htm` and `weapons.htm`. `tools/web_bases.py` does it at publish time
(`scripts/publish-overlay.ps1` downloads the pages into data/website-bases/; skip with -SkipWebsiteBases), and
`src/web_bases_live.cpp` with `src/web_pages_updater.cpp` redo it in the game (cached next to the data). The pictures,
tiers and weapon families the website does not give come from data/web-base-art.txt, which the extractor writes from the
mod's tables. A file named `bases-live-disabled.txt` in the data folder turns the in-game check off. Both builders must
agree; `web_bases_live_test` checks that.

## Ascendancies from the website
The Ascendancy tab is rebuilt from `ascendancies.htm` on the website: the introduction, the challenge texts, the gems that
unlock each ascendancy, every tier's stats and the footnotes. `tools/web_ascendancy.py` does it at publish time
(`scripts/publish-overlay.ps1` downloads the page into data/website-ascendancy/; skip with -SkipWebsiteAscendancy), and
`src/web_ascendancy_live.cpp` with `src/web_pages_updater.cpp` redo it in the game (cached next to the data). The layout
is the existing one (Ascendancies, Higher Tier Challenges, one entry per ascendancy with its picture). The Orb of
Creation table is not on the website, so it is kept from the existing page. A file named `ascendancy-live-disabled.txt` in
the data folder turns the in-game check off. Both builders must agree; `web_ascendancy_live_test` checks that.

## Set Items from the website
The Set Items tab is rebuilt from `sets.htm` on the website: every set piece's name, base, levels, stats and item-count
bonuses, and each set's partial and full bonuses. `tools/web_sets.py` does it at publish time (`scripts/publish-overlay.ps1`
downloads the page into data/website-sets/; skip with -SkipWebsiteSets), and `src/web_sets_live.cpp` with
`src/web_pages_updater.cpp` redo it in the game (cached next to the data). The website has no pictures, so each card keeps
the picture the existing page gives it, matched by title and difficulty. A file named `sets-live-disabled.txt` in the data
folder turns the in-game check off. Both builders must agree; `web_sets_live_test` checks that.

## Endgame Maps from the website
The Endgame Maps tab is rebuilt from `endgame_maps.htm` on the website: the overview paragraphs, and each of the 18 map
bosses with its description, abilities and rewards. The website's colours become the overlay's colour letters.
`tools/web_endgame.py` does it at publish time (`scripts/publish-overlay.ps1` downloads the page into
data/website-endgame/; skip with -SkipWebsiteEndgame), and `src/web_endgame_live.cpp` with `src/web_pages_updater.cpp`
redo it in the game (cached next to the data). The website has no boss pictures, so each boss keeps the picture the
existing page gives it, matched by title. A file named `endgame-live-disabled.txt` in the data folder turns the in-game
check off. Both builders must agree; `web_endgame_live_test` checks that.

## Special Craft from the website
The Special Craft tab (corruptions, anointments, sanctifications and standard of heroes upgrades) is rebuilt from
`special_crafting_outcomes.htm` on the website by `tools/web_special.py`. Each section's intro becomes its text, each
input (an item type or an item name) becomes a block, and each outcome row becomes one `Same Item: ...` line. The
overlay's `web_special_live.cpp` does the same at runtime, so the tab refreshes like the other website pages; a file
named `special-live-disabled.txt` turns the check off. `publish-overlay.ps1 -SkipWebsiteSpecial` leaves the saved copy
and the page as they are.

## Mercenary/O-Skill from the website
The Mercenary/O-Skill tab is rebuilt from `skill_information.htm` on the website by `tools/web_skills.py`: the intro, the
mercenary table (hireling, subtype, skills) and the O-skill table (skill, description). Each table becomes an entry with
its column headings and one row per line. The overlay's `web_skills_live.cpp` does the same at runtime, so the tab
refreshes like the other website pages; a file named `skills-live-disabled.txt` turns the check off.
`publish-overlay.ps1 -SkipWebsiteSkills` leaves the saved copy and the page as they are.

## Vessel of Souls from the website
The Vessel of Souls tab is rebuilt from `vessel_of_souls.htm` on the website by `tools/web_vessel.py`: the overview, the
four tier tables (organs, effect and soul cost) and the completion table. The website's colours become the overlay's
colour letters, and a tier's soul cost comes from the intro ("consumes N souls per use") unless a row states its own.
A row with an input but no outcome is skipped. The overlay's `web_vessel_live.cpp` does the same at runtime; a file named
`vessel-live-disabled.txt` turns the check off. `publish-overlay.ps1 -SkipWebsiteVessel` leaves the saved copy and the page
as they are.

## Kill Ledger from the website
The Kill Ledger tab is rebuilt from `kill_ledger.htm` on the website by `tools/web_kill.py`: the overview, the kill bonus
table (kills, gem, bonus) and the boss bonus table (ingredients, boss, bonus). The website's colours become the overlay's
colour letters. Maple Leaf style rows keep each ingredient beside its gem; a boss row's last line is the boss, with " just
killed" removed. The overlay's `web_kill_live.cpp` does the same at runtime; a file named `kill-live-disabled.txt` turns the
check off. `publish-overlay.ps1 -SkipWebsiteKill` leaves the saved copy and the page as they are.

## Weapon Mastery from the website
The Weapon Mastery tab is rebuilt from `weapon_mastery.htm` on the website by `tools/web_mastery.py`: the overview, the general
recipes (ingredients, result) and one entry per specialization variant (skill, effect). Like the cube recipes and the Vessel of
Souls, each ingredient sits on its own line. The website's colours become the overlay's colour letters, and a variant's skill
is the all-orange line that starts it. The overlay's `web_mastery_live.cpp` does the same at runtime; a file named
`mastery-live-disabled.txt` turns the check off. `publish-overlay.ps1 -SkipWebsiteMastery` leaves the saved copy and the page
as they are.

## Item affixes from the website
The Item Affixes page exists only on the website: `prefixes.htm`, `suffixes.htm` and `quality_affixes.htm`.
`tools/web_affixes.py` builds it at publish time (`scripts/publish-overlay.ps1` downloads the pages into
data/website-affixes/; skip with -SkipWebsiteAffixes), and `src/web_affixes_live.cpp` with `src/web_pages_updater.cpp`
redo it in the game (cached next to the data). Three entries: Armor, Weapons and Other (General, jewelry, charms, quivers and any type not listed in `ARMOR` or `WEAPONS` in web_affixes.py / web_affixes_live.cpp). Each holds one block per item type and kind; the item type is the block's Type filter and the kind (Prefix, Suffix, Superior - the quality page) its Affix filter. General, voib and the combined quiver table are left out; Amulet follows Ring and Jewel follows Small Charm. A file named `affixes-live-disabled.txt` in the
data folder turns the in-game check off. Both builders must agree; `web_affixes_live_test` checks that.

## IAS calculator from the website
The IAS Calculator tab (second row) is the website's Frame Table. `src/ias_calc.cpp` is a port of the page's own script
(formulas, notes and tables) with no Win32 in it; `src/overlay_calc.inc` draws it in the overlay. Its data comes from two
saved website files, `ias-calculator.html` (the tunable constants at the top of its script) and `ias-calculator-data.js`
(classes, forms, weapons, animation frames), kept in data/website-ias/ and shipped next to wiki-data.txt. The game
reads the shipped copy at start and refreshes it from the website like the other pages (a file named
`ias-live-disabled.txt` turns that off); `scripts/publish-overlay.ps1` refreshes the saved copies (-SkipWebsiteIas).
A change to the data or the constants reaches the game by itself; a change to the formulas on the website needs this
port changed. `tests/ias_calc_test.cpp` checks the port against the page's script: `node tests/gen_ias_cases.js
<ias-calculator.html> <ias-calculator-data.js> tests/ias_cases.tsv 400` regenerates the cases (`SEED=n` changes them).

## Languages
`data/locales/<code>/` (deDE, esES, esMX, frFR, itIT, ptBR, plPL, ruRU, koKR, jaJP, zhCN, zhTW) holds `ui-strings.txt`
(hand-written labels), `wiki-data.txt` and `cube-wiki-data.txt` (offline fallbacks). In game the pages are rebuilt from the
localized website pages (`locale_align.cpp`, `locale_live.cpp`). `deploy.cmake` and the install rules copy the folder; see
`overlay/LOCALIZATION-NOTES.md` for the method and for what was and was not checked in the game (zhCN, zhTW and jaJP were not).

## Known limits

- One gem icon (`items/misc/jewel/jewel`) is missing from the mod and base game; the
  Gemwords card that uses it shows the Smiiiff Stone art instead.
- Colors are approximations of the D2 palette. Stat lines use the first color code on each line.
