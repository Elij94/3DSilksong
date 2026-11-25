# Changelog

## 0.7.0 — Hornet moves as in the game

- Her values read from the game's own files: run speed, jump speed and timing, the slower gravity at the top of a
  jump while B is held, gravity 60, fall speed, dash and sprint speeds, attack timing.
- Ledge grab: against a wall whose top is within reach, Hornet hangs on, then vaults onto it; B jumps out of it.
- The down attack is the Hunter crest's down spike: a short stop, a dive forward and down, a bounce when it hits.
- Sprint eases from the dash speed down to sprint speed and skids to a stop; air dash glides on.
- Smoother motion: the game moves by whole screen refreshes instead of the jittery millisecond clock.

## 0.6.1

- The top screen in 400x240: in the 800x240 mode only the left half of the picture reached the screen, twice as
  wide (the view looked zoomed in and Hornet sat on its right edge). Azahar does show the mode, so it is now tested
  as the console runs it.
- The view zoomed out a little: about 19 units of height instead of 17.

## 0.6.0 — playable

- Drawing: textures the right way up (the converted data is mirrored on the GPU, and citro2d takes a subtexture
  whose top is below its bottom as rotated); tiles on shared pixel edges (no more bright or dark seams); tiles and
  big sprite pages shrunk to what the screen shows (about 4 times less memory).
- Hornet: Hollow Knight style jump, sprint, silk and bind, benches; the camera centred on her, lock areas sorted out,
  respawn markers used as her origin.
- Enemies that move, chase, take knockback and leave corpses; ambient particles; the game's HUD; music and
  ambience per room; title screen and pause menu. Crash on quit fixed.
- Converter: giant black-tinted sprites (rain sheets, fades) and hidden-area masks are left out of the scenery
  (whole rooms were black); music as PCM and a room -> music map.
- Tools: a reference renderer, scripted tests in the Azahar emulator.

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
