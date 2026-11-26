# 3DSilksong

Tools that convert **Hollow Knight: Silksong** into data for the **New 3DS / New 2DS XL** (400x240 top screen,
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
   - **music and sound** (`build_music.py`, `build_music_pcm.py`, `music_map.py`): music and ambience cues mixed
     down to Ogg Vorbis and to PCM for the engine, which room plays which, and every sound the state machines play,
     with an index that links them to their actions;
   - **text** (`build_text.py`): the game's language sheets, decrypted, as `KEY<TAB>text` files.
3. **Package**: everything goes into one RomFS file, `3dsilksong.romfs`, to copy to `SD:/3ds/3dsilksong/`.

`tools/build.sh` runs it all. The formats are described in [docs/FORMATS.md](docs/FORMATS.md), and
`tools/inspect_data.py` reads back and checks the files produced.

`source/` is the 3DS engine that reads these files (see [The engine](#the-engine)).

## The engine

`make` builds `3dsilksong.3dsx` (C, libctru + citro2d, no other library). Copy it with the data file to
`SD:/3ds/3dsilksong/` and start it from the Homebrew Launcher on a New 3DS. Music needs the DSP firmware dump,
`SD:/3ds/dspfirm.cdc` (DSP1); without it the game runs silent.

What it does (0.15.0):
- **Rooms** (`room.c`): the collision grid, gates, hazards, respawn points and camera locks; the scenery layers with
  their parallax and blending, their tiles inflated (`inflate.c`, a zlib decoder of its own) and shrunk to what the
  screen shows of their layer, into a texture cache as the camera reaches them.
- **Sprites** (`sprite.c`): `.sss` sets with tk2d clips; a page is inflated into the GPU when a frame on it is first
  drawn, the least recently drawn pages go when linear memory runs out.
- **Hornet** (`hero.c`, animations from `sprites/knight.sss`): moves with her own values from the game (her
  HeroController, the Hunter crest, her FSMs): run, jump (held rise, float at the top, release halves the rise), dash
  into sprint with skids and turns, air dash glide, ledge grab, down spike, side and up needle attacks, silk and bind,
  and the abilities found later, all unlocked: wall cling and wall jump, double jump, float, Clawline, Silk Soar.
- **Enemies** (`enemy.c`): a behaviour picked from each one's animations while the FSMs don't run: walkers patrol
  and charge, flyers hover and chase; knockback, death animation and corpse.
- **Objects** (`objects.c`), **particles** (`particles.c`): drawn at their depth between the scenery layers; the
  ambient emitters that loop.
- **State machines** (`fsm.c`): the rooms' PlayMaker FSMs, compiled by `tools/build_fsm.py`, run with about a
  hundred of the game's actions: breakable walls, gates and scene events follow the game's own logic, walls that
  open leave the collision grid, PlayerData flags are saved. Enemies run theirs too, with bodies that fall and collide
  and the game's walking components (Walker, WalkerV2, Crawler).
- **Hits and kills** (`objects.c`, `enemy.c`, `fx.c`): as HealthManager does them: flash, the enemy's recoil, its hit
  and death effects and sounds (`fx/`, `tools/build_fx.py`), freeze moments, the corpse flung and landing.
- **Dialogues** (`dialogue.c`): the NPCs' lines in the game's text (French on a French console), page by page;
  Up near an NPC or anything waiting for it starts it.
- **Music and sounds** (`audio.c`): each room's music and ambience (`music/rooms.txt`), streamed as PCM, with fades;
  the FSMs' sound effects on 8 more channels, by distance.
- **Rosaries and shell shards** (`currency.c`): flung out of kills, picked up, counted in PlayerData.
- **Game** (`game.c`, `menu.c`): the title screen from the game's Menu_Title scene, its menus in its own text and
  fonts, 4 save slots (`SD:/3ds/3dsilksong/save<N>.txt`), options, the pause menu, the camera framing, gates between
  rooms, the game's HUD at the game's size (bind orb, masks, silk spool, counters), benches.
- **Spawned prefabs** (`fsm.c`, `tools/build_prefabs.py`): projectiles, effects and summons from the game's pool.
- **Map** (`map.c`, `tools/build_map.py`): on the bottom screen once the area's map is bought, rooms mapped at a
  bench with the quill, as the game's GameMap does; before that the area's name.
- **Shops** (`shop.c`, `tools/build_shops.py`): the merchants' stocks, bought with rosaries or shards.

Controls (the Switch version's): pad or Circle Pad to move (and look up/down), B jump, Y needle (up+Y, down+Y in
the air), ZR dash (held on: sprint), ZL Clawline, X Needolin, A bind, Up for doors, benches, NPCs and shops (A: next
line, buy; B: leave), START pause. Abilities come as the game gives them.

Testing switches on the SD card, in `SD:/3ds/3dsilksong/`: `allabilities`, `allmaps`, `nowide`, `nofsm`,
`autotest.txt` (scripted input, see `source/autotest.c`).

Not there yet: the inventory, tools and crests, map pins and markers, bosses' special parts, cutscenes.

### Testing in an emulator

`tools/emu_run.ps1` runs the engine in [Azahar](https://github.com/azahar-emu/azahar) (portable, in `../Azahar`)
without taking the focus and saves a screenshot of its window. With `SD:/3ds/3dsilksong/autotest.txt` the engine
reads its input from a script and writes Hornet's state and the FSMs' transitions to `log.txt` (see
`source/autotest.c`); files under `SD:/3ds/3dsilksong/override/` replace the data file's (a room's `.ssf`, say). `tools/render_ref.py` draws a room from the converted files with the
engine's math, to tell an engine bug from a data one.

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
source/        the 3DS engine (rooms, sprites, Hornet, objects, game loop)
cia/           the CIA's RSF (for `make cia`, add icon.png 48x48, banner.png 256x128 and banner.wav, your own)
docs/          EXPLORATION.md (what the game holds), FORMATS.md (the files produced)
```

## Known limits

- The engine runs about 200 of the game's script actions (see [The engine](#the-engine)); the others are skipped.
  Shop items that give tools or collectables are counted in PlayerData until those systems exist; the ones needing
  another item or a quest are not offered yet. Tested on a New 3DS and in Azahar.
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
