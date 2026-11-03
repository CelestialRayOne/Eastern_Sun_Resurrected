# ESR Wiki Overlay 1.0.0

A read-only in-game wiki overlay for Eastern Sun Resurrected (ESR) on Diablo II
Resurrected, loaded by D2RLoader. It has two windows: the ESR Wiki and Horadric Cube Recipes.

Author: Sully

## Install

Copy these files into your mod's D2RLoader plugins folder:

```
Eastern_Sun_Resurrected/d2rloader/plugins/
  d2rl-esr-wiki-overlay.dll
  esr-wiki-overlay/
    wiki-data.txt
    cube-wiki-data.txt
    images/*.png
```

If you previously used the native ESR wiki plugin, remove its files
(`d2rl-esr-wiki.dll` and `d2rl-esr-wiki.mpq`) from the same folder so the two do not both load.

## Open it

- Type `eswiki` in the in-game console, or
- Bind a key to "Open ESR Wiki" in the game's controls (no key is bound by default).

## Notes

- The overlay only reads the data files above. It does not change the game.
- Colors and stat lines follow the game's text color codes.
