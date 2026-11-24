# Changelog

## 0.2.0 — first engine

- `source/`: a 3DS engine that reads the converted data: streamed parallax scenery with normal / additive / multiply
  layers, collisions, gates between rooms with fades, camera locks; Hornet (run, jump, dash, needle attacks in three
  directions, damage, hazard respawn, death); room objects animated at their depth, enemies and breakables that
  take hits, contact damage; room menu, automatic save, status on the bottom screen.
- Its own zlib decoder (`inflate.c`), so no portlib is needed; 48 MB of linear memory for the texture caches.

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
- Particles (`particles.py` per room, `build_particles.py` for their textures), interface atlases (`build_ui.py`),
  TextMeshPro fonts as SDF A8 atlases (`build_font.py`), every sound the FSMs play with a guid index
  (`build_music.py --fsm-sfx`).
- `source/`: 3DS skeleton that mounts the data file from the SD card and sets the 800x240 mode (not compiled yet).
