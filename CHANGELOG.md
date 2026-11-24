# Changelog

## 0.1.0 — first version of the conversion tools

- Export: AssetRipper driven headless (`tools/export_game.sh`), with `DefaultVersion = 6000.0.50f1` (the
  Addressables bundles carry no engine version); inventory of the install (`inventory.py`); bundle dependencies
  and small test exports (`bundle_deps.py`); export index (`index_export.py`).
- Rooms (`build_room.py`): collision grid, gates, respawn points, hazards, camera locks, parallax scenery in
  normal / additive / multiply layers as 3DS GPU tiles; room objects (`objects.py`); PlayMaker FSMs with templates
  (`fsm_extract.py`, from HK3DS).
- Sprites (`build_sprites.py`): tk2d animation libraries to GPU pages and clips, including every library the room
  objects use.
- Music and sound (`build_music.py`), language sheets (`build_text.py`).
- One RomFS file (`tools/mkromfs.py` when `mkromfs3ds` isn't installed); everything chained, resumable and in
  parallel by `tools/build.sh`; files checked by `inspect_data.py`. Formats in `docs/FORMATS.md`.
- `source/`: 3DS skeleton that mounts the data file from the SD card and sets the 800x240 mode (not compiled yet).
