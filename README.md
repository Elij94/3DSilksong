# 3DSilksong

**A Hollow Knight: Silksong engine for the New 3DS**, with the tools that convert **your own PC copy** of the game
into data it can run. Hornet, the rooms, the enemies and the game's own scripts run natively on the console: it is
not an emulator and not a video.

![version](https://img.shields.io/badge/version-0.20.0-blue)
![progress](https://img.shields.io/badge/progress-65%25-orange)
![platform](https://img.shields.io/badge/platform-New%203DS%20%2F%20New%202DS%20XL-lightgrey)
![license](https://img.shields.io/badge/license-MIT-green)

> Unofficial fan project, **not affiliated with Team Cherry**. This repository holds **no game file** (no image,
> sound, text or data): only code. The tools convert a copy of Silksong **you own**, on your own machine.
>
> **Do not share what the tools produce** (`3dsilksong.romfs`, `romfs/`, the exports): those files hold the game's
> art, music and text. Everyone converts their own copy. Issues and pull requests that contain or ask for game files
> will be closed.

## Progress

<img src="docs/progress/overall.svg" width="480" height="18" alt="65 %"> &nbsp;**65 %** overall

| Part | Progress | | Where it stands |
|---|---|--:|---|
| Data conversion (tools/) | <img src="docs/progress/95.svg" width="160" height="10" alt="95 %"> | **95 %** | 590 rooms, ~17,000 scripts, 409 sprite sets, music, sounds, text, fonts |
| Rooms and scenery | <img src="docs/progress/95.svg" width="160" height="10" alt="95 %"> | **95 %** | Parallax layers streamed from memory, collisions, gates, room entries, water |
| Hornet | <img src="docs/progress/85.svg" width="160" height="10" alt="85 %"> | **85 %** | Movement with the game's values, needle attacks, dash/sprint, bind, all abilities, swimming |
| Game scripts (PlayMaker) | <img src="docs/progress/91.svg" width="160" height="10" alt="91 %"> | **91 %** | 964,000 of the rooms' 1,062,000 actions run; the rest are mostly sound and visual effects |
| Enemies | <img src="docs/progress/80.svg" width="160" height="10" alt="80 %"> | **80 %** | Their own scripts, walking/crawling/flying components, hits, recoil, deaths, corpses |
| Bosses | <img src="docs/progress/30.svg" width="160" height="10" alt="30 %"> | **30 %** | Additive boss scenes load; Moss Mother and the Bell Beast fight; the others are untested |
| Menus, saves, HUD | <img src="docs/progress/85.svg" width="160" height="10" alt="85 %"> | **85 %** | Title and profile screens as the game's, 4 save slots, pause, options, HUD |
| Map, inventory, shops, items | <img src="docs/progress/70.svg" width="160" height="10" alt="70 %"> | **70 %** | Map and inventory on the bottom screen, shops, items picked up in the rooms |
| Tools and crests | <img src="docs/progress/40.svg" width="160" height="10" alt="40 %"> | **40 %** | Slots, equipping at benches, red tools thrown with R; passive tools and crest changes missing |
| NPCs and quests | <img src="docs/progress/40.svg" width="160" height="10" alt="40 %"> | **40 %** | Dialogues in the game's text, scripted events; quest tracking partial |
| Audio | <img src="docs/progress/80.svg" width="160" height="10" alt="80 %"> | **80 %** | Room music and ambience, script sounds by distance |
| Cutscenes | <img src="docs/progress/0.svg" width="160" height="10" alt="0 %"> | **0 %** | The game's videos are not converted |

The overall figure is a weighted estimate of how much of the game can be played as on PC. See
[CHANGELOG.md](CHANGELOG.md) for what each version brought.

## What it is

Two parts:

1. **`tools/`** (Python): read the game's files exported by AssetRipper and turn them into compact formats for the
   3DS: rooms cut into GPU tiles, objects, PlayMaker state machines compiled to bytecode, tk2d sprite sets, particles,
   fonts, text, music and sounds. Everything ends up in one file, `3dsilksong.romfs` (about 2.4 GB).
2. **`source/`** (C, libctru + citro2d): the engine. It reads those files and runs the game: Hornet's controller with
   her values from the game, the rooms' and enemies' PlayMaker scripts (about 300 of the game's action types), the
   physics, the camera, the HUD, menus and saves. The top screen runs in the New 3DS's 800-pixel mode; the bottom
   screen holds the map and the inventory.

## Playing

You need a **New 3DS or New 2DS XL** with the Homebrew Launcher, and a PC copy of Silksong.

1. Convert your copy (see [Building the data](#building-the-data)); this gives `3dsilksong.romfs`.
2. Build the engine with `make` (or take `3dsilksong.3dsx` from a release, when there is one).
3. Copy both to `SD:/3ds/3dsilksong/` and start **3DSilksong** from the Homebrew Launcher.
   Music needs the DSP firmware dump `SD:/3ds/dspfirm.cdc`; without it the game runs silent.

Controls (as the Switch version's): pad or Circle Pad to move, **B** jump, **Y** needle (with up/down), **ZR** dash
(held: sprint), **ZL** Clawline, **X** Needolin, **A** bind, **R** throw a tool (with up/down), **Up** for doors,
benches, NPCs and shops, **SELECT** inventory (L/R: Inventory / Tools tab), **START** pause. The pause menu has a
**Tests** page (abilities, rosaries, shards, maps, tools, health) for trying things out.

## Building the data

Requirements:
- A PC copy of Silksong (Steam or Game Pass).
- Python 3.11+ with [requirements.txt](requirements.txt), and `ffmpeg` on PATH.
- [AssetRipper](https://github.com/AssetRipper/AssetRipper) 2.0.0 (on PATH or given by `ASSETRIPPER`).
- `mkromfs3ds` (devkitPro) or `tools/mkromfs.py` (included).
- About 60 GB of disk and 16 to 24 GB of free memory during the export.

```sh
export SILKSONG="C:/XboxGames/Hollow Knight- Silksong"   # the game's install folder
tools/build.sh                                             # export, conversion, RomFS: everything
```

A full conversion takes about 1.5 hours on a 16-thread PC. `tools/build.sh` resumes where it stopped. On Windows the
scripts run in Git Bash. Step-by-step commands, options and the file formats are in
[docs/FORMATS.md](docs/FORMATS.md) and [docs/EXPLORATION.md](docs/EXPLORATION.md).

## Building the engine

devkitPro with `3ds-dev`, then `make` in the repository. `make cia` builds a CIA (with `makerom` and `bannertool`, and
your own `cia/icon.png`, `banner.png` and `banner.wav`).

## Testing without a console

The engine runs in the [Azahar](https://github.com/azahar-emu/azahar) emulator. `tools/emu_run.ps1` starts it and
takes a screenshot. With `SD:/3ds/3dsilksong/autotest.txt` the engine reads its input from a script (warps to rooms,
held buttons, screenshots) and logs Hornet's state, the enemies and the scripts' transitions to `log.txt` (see
`source/autotest.c`). That is how most of the work is checked.

## How to help

The project is paused, and help is very welcome. The most useful things, roughly in order:

- **Bosses**: play each boss fight, compare with the PC game, and fix what differs. Most of them run on PlayMaker
  actions the engine already knows; the log shows which states they reach.
- **Missing script actions**: `python tools/build_fsm.py romfs` prints the actions it skips, most frequent first.
  Each one is a `case` in `source/fsm.c`, written from the game's C# (decompiled by AssetRipper).
- **Tools**: the passive (blue and yellow) tools' effects, the red tools without a projectile, changing crests.
- **NPCs and quests**: quest tracking, the journal, the quest board.
- **Testing on hardware**: performance in the heavy rooms, anything that looks different from the PC game.
- **Cutscenes**: converting the game's videos to something the 3DS can play.

Before starting on something big, open an issue to say so. Keep pull requests free of any game file.

## Layout

```
tools/        export, conversion and packaging (Python, bash)
source/       the 3DS engine (C)
cia/          the CIA's RSF
docs/         FORMATS.md (the files produced), EXPLORATION.md (what the game holds)
```

By size: **C 64 %** (the engine, about 510 KB), **Python 33 %** (the tools, about 265 KB), shell, Makefile and
PowerShell 3 %.

## License

MIT for the code of this repository (see [LICENSE](LICENSE)). Part of the tools' structure (Makefile, RSF, the
textures' Morton order, `sscommon.py`, `index_export.py`, the text decryption, `fsm_extract.py`) is adapted from
[HK3DS](https://github.com/silviognotti/HK3DS), also under the MIT license. Hollow Knight: Silksong belongs to
Team Cherry.
