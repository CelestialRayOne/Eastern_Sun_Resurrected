# D2RLoader files

This folder holds the D2RLoader files used by this mod.

* metadata.json stores basic mod information and the D2RLoader version it was made for.
* `config/d2rloader.toml` lets this mod override some D2RLoader settings while it is active.
* `plugins/` can contain D2RLoader plugin DLLs used only by this mod.
* `patches/` can contain memory patch files.
* `logs/` stores log files created by mod plugins.
* Use the MPQ viewer to browse D2RLoader reference data in `plugins/d2rloader.mpq`. Copy only the resources you want to override into your mod's `.mpq` folder.
* Add an empty `data/global/.DISABLE_DEBUG` file to disable D2RLoader debug tools while the mod is active. Useful for packed `.mpq` mods where the marker cannot be removed as a loose file.

Keep this folder next to the mod’s `.mpq` folder when sharing the mod.

Run `d2rl update` to restore any missing D2RLoader files. Existing files will not be overwritten.

This README can be safely deleted.
