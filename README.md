# 3DSilksong

Tools that convert **Hollow Knight: Silksong** into data for the **New 3DS / New 2DS XL** (800x240 top screen,
124 MB of RAM), modelled on the tools of [HK3DS](https://github.com/silviognotti/HK3DS).

> Unofficial project, **not affiliated with Team Cherry**. This repository holds **no game file** (no image, sound,
> text or data): only tools that convert **your own PC copy** of the game, on your own machine. You need to own
> Silksong to use them.
>
> **Do not share what the tools produce** (`3dsilksong.romfs`, `romfs/`, the exports): those files hold the game's
> art, music and text. Everyone converts their own copy. Issues and pull requests that contain or ask for game files
> will be closed.

## What the tools do

1. **Export**: `tools/export_game.sh` drives AssetRipper (headless) on the `_Data` folder of your copy.
2. **Conversion** (`tools/`, Python):
   - **rooms** (`build_room.py`): collision grid, gates (`TransitionPoint`), respawn points, hazards, camera locks,
     and the scenery drawn in parallax layers (normal, additive, multiply), cut into 3DS GPU tiles;
   - **objects** (`objects.py`, called by `build_room.py`): enemies, breakables, NPCs and interactables, animated
     objects, with their position, HP, damage, tk2d animation and FSMs;
   - **PlayMaker state machines** (`fsm_extract.py`, called by `build_room.py`): states, transitions, decoded actions
     and variables, templates (`FsmTemplate`, `RunFSM`) included;
   - **sprites** (`build_sprites.py`): any tk2d animation library as GPU texture pages with its clips, including every
     library the room objects use;
   - **particles** (`particles.py`, called by `build_room.py`, and `build_particles.py` for their textures): every
     emitter of the rooms with its shape, rate, lifetime, speed, size and colours;
   - **interface** (`build_ui.py`): the sprite atlases of the HUD, inventory, map, menus, prompts, journal…, one named
     sprite per entry;
   - **fonts** (`build_font.py`): the game's TextMeshPro fonts, glyphs and signed-distance-field atlas;
   - **music and sound** (`build_music.py`): music and ambience cues mixed down to Ogg Vorbis, and every sound the
     state machines play, with an index that links them to their actions;
   - **text** (`build_text.py`): the game's language sheets, decrypted, as `KEY<TAB>text` files.
3. **Package**: everything goes into one RomFS file, `3dsilksong.romfs`, to copy to `SD:/3ds/3dsilksong/`.

`tools/build.sh` runs it all. The formats are described in [docs/FORMATS.md](docs/FORMATS.md), and
`tools/inspect_data.py` reads back and checks the files produced.

`source/` holds only a 3DS skeleton: it mounts the data file from the SD card (`romfsMountFromFile`) and switches the
top screen to 800x240. It is the starting point of an engine that would read these formats.

## Requirements

- A PC copy of Silksong (Steam or Game Pass; Game Pass files are readable).
- Python 3.11+ with the packages of [requirements.txt](requirements.txt) (`python -m pip install -r requirements.txt`);
  `ffmpeg` on PATH.
- [AssetRipper](https://github.com/AssetRipper/AssetRipper) 2.0.0, on PATH or given by `ASSETRIPPER`.
- For the RomFS file: `mkromfs3ds` (devkitPro) if installed, else `tools/mkromfs.py` (Python, included).
- To build `source/`: devkitPro with `3ds-dev` (and `makerom`, `bannertool` for the CIA).
- Disk: ~60 GB for the full export. Memory: 16 to 24 GB free during the AssetRipper export.

## Usage

```sh
export SILKSONG="C:/XboxGames/Hollow Knight- Silksong"   # the install folder
tools/build.sh                                             # everything: inventory, export, index, conversion, RomFS
```

Or step by step:

```sh
python tools/inventory.py "$SILKSONG"                      # a few-second overview, no export needed
tools/export_game.sh "$SILKSONG"                           # AssetRipper export into ../ssdata (long)
python tools/index_export.py
python tools/build_room.py romfs Tut_01 --preview tut01.png   # one room (and a picture of its geometry)
python tools/build_sprites.py --list                       # the animation libraries of the export
python tools/build_sprites.py romfs/sprites/hero.sss "Hornet Anim"
python tools/build_music.py romfs
python tools/build_text.py romfs EN FR
python tools/inspect_data.py romfs/rooms/Tut_01.ssr romfs/sprites/hero.sss
```

To try a single room without exporting everything (~700 MB, a few minutes):

```sh
python tools/bundle_deps.py --depth 1 --extra hero --subset ../test_tut01 Tut_01
SSDATA=../test_tut01_out tools/export_game.sh ../test_tut01
echo Tut_01 > rooms.txt
SSDATA=../test_tut01_out tools/build.sh --rooms rooms.txt
```

On Windows the scripts run in Git Bash. `SSDATA` sets the exports folder (default `../ssdata`, next to the
repository). That folder, `work/`, `romfs/` and everything produced are ignored by git.

### Good to know

- Silksong's bundles don't record their Unity version: `export_game.sh` sets `DefaultVersion = 6000.0.50f1` in
  AssetRipper, without which no room gets exported (see [docs/EXPLORATION.md](docs/EXPLORATION.md)).
- The options of `build_room.py` (`--ppu`, `--cpu`, `--fmt`) set the detail of the scenery and collisions, and so the
  size of the data. With the defaults, `Tut_01` has 6.1 MB of scenery (1184 RGBA4 tiles).
- A full conversion, roughly (16 threads, 32 GB): export ~15 min, rooms ~30 min with `JOBS=6`. Result: 590 rooms,
  575 with particles, ~17,000 FSMs, 397 sprite sets, 56 interface atlases, 11 fonts, 2071 sounds, 109 music tracks,
  a RomFS of about 2.25 GB (under the 4 GB per-file limit of FAT32 SD cards).
- `tools/build.sh` resumes where it stopped; a room that fails is listed in `work/rooms_failed.txt` and doesn't stop
  the rest.

## Layout

```
tools/         export, conversion, packaging (Python / bash)
tools/data/    all_rooms.txt (the 590 scenes), sprites.txt (sprite sets on top of the objects' ones)
source/        3DS skeleton (data mounting, HD screen)
cia/           the CIA's RSF (for `make cia`, add icon.png 48x48, banner.png 256x128 and banner.wav, your own)
docs/          EXPLORATION.md (what the game holds), FORMATS.md (the files produced)
```

## Known limits

- There is no game engine: the data is ready, but nothing on the 3DS reads it yet. `source/` is a skeleton that has
  not been compiled yet.
- Cutscenes (videos) are not converted.
- Particles: Unity's curves (speed, size, colour over lifetime) are reduced to their start and end values; the rarer
  modules (noise, collisions, sub-emitters, trails) are not read.
- Fonts: the Chinese, Japanese and Korean fonts are only converted with `FONTS_ALL=1`.
- A few animation frames can't be cut out of their atlas (they are `-1` in the clips), and two FSMs out of about
  17,000 can't be decoded (they are marked `error`).
- Tested with the Game Pass version of the game (Unity 6000.0.50f1) on Windows 11 and Git Bash.

## License

MIT for the code of this repository (see [LICENSE](LICENSE)). Part of the structure (Makefile, RSF, the textures'
Morton order, `sscommon.py`, `index_export.py`, the text decryption, `fsm_extract.py`) is adapted from HK3DS, also
under the MIT license. `tools/mkromfs.py` replaces devkitPro's `mkromfs3ds`. Hollow Knight: Silksong belongs to
Team Cherry.
