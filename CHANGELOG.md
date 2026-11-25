# Changelog

## 0.12.0 — the sharp 800-pixel top screen, the game's bind, the bench without a stall

- The top screen in the 800x240 mode on the New 3DS (twice the pixels across; no 3D): the render target is made at
  the framebuffer's real size (citro2d's own is 400 wide). `SD:/3ds/3dsilksong/nowide` keeps 400x240 (emulators
  show the 800 mode stretched).
- The bind as the Bind FSM does it: Bind Time 1.2 s, a voice line, the silk taken off at the start (the spool's
  chunks go down), bind_pt_1 and a shake, the Bind Silk round her at its place and size, its loop only while
  binding, then the heal: a white flash, a shake, the Bind Flash burst, bind_pt_2; the masks given back play their
  Health Refill. Looping sounds (`sfx_loop_name`, `sfx_stop`).
- The bench saves once Hornet is seated: writing the card no longer stalls her sitting down.

## 0.11.0 — the game's HUD at its size, rosaries and shell shards, fixes from the console

- The HUD placed and sized as the game's HudCamera shows it (13.8 pixels a unit, anchored top left): the bind orb,
  the masks at 0.71, and the real silk spool (SilkSpool: rod between its caps, a chunk every 0.18, glowing when a bind
  is ready). Rosary and shell shard counters where the game puts them, a few seconds after a pickup.
- Kills fling the enemy's rosaries and shell shards (HealthManager's drop counts and fling); they fall, rosaries bounce
  (Rosary Bead: 0.5), Hornet picks them up 0.35 s after they appear, with their sounds; totals in PlayerData
  `geo` / `ShellShards` (saved). `tools/build_items.py` makes their sprites from the prefabs' animator clips.
- Enemies were doubled by a still copy baked into the scenery: sprites with a tk2d animator are left to the engine.
- `TestGameObjectActivator`: rooms with a normal and a Black Thread variant now show the normal one (its enemies were
  missing).
- Hornet: the sprint turn flips her at once and skids the old way while "Sprint Turn" plays (Sprint FSM), with
  its sound; the wall slide no longer holds when pushing towards the wall; the dash plays the hero's dash sound; the
  bind shows the "Bind Silk" wrapping round her, then a white flash; the bench: she is moved onto the seat while "Sit"
  plays, the rest flashes, "Sit Idle", and "Get Off" when she stands up.
- Bottom screen: it no longer scrolls every frame (it flickered between two layouts and hid the frame rate).

## 0.10.0 — enemies on their own scripts, hits and kills as in the game

- Enemies run their own FSMs: alert ranges, raycasts, collisions, facing, chasing, flying, and the game's walking
  components (Walker, WalkerV2, Crawler); bodies have gravity and collide with the room.
- A hit does what HealthManager does: the FSM events (HIT, TOOK DAMAGE, RECOIL...), the white flash, the enemy's own
  recoil, its hit effects (particles turned to the hit, damage sound). A kill: the death freeze, the camera shake, the
  enemy's death sounds and effects, its corpse flung at the game's angles and speed, Death Air then Death Land.
- Hornet hurt: the game's damage freeze.
- Killed enemies that the game remembers stay dead (the semi-persistent ones come back after a bench rest); objects
  switched off by PlayerData are.
- Multiply layers premultiplied: no more red-saturated rooms.
- `tools/build_fx.py`: the 58 hit and death profiles, their particles and sounds.

## 0.9.0 — the game's scripts run

- A PlayMaker runtime (`fsm.c`): `tools/build_fsm.py` compiles each room's state machines and the scene's
  hierarchy into `rooms/<Scene>.ssf`; the engine runs them with about a hundred of the game's actions (flow, events,
  variables, objects on and off, positions and movement, tk2d animations, Hornet's triggers, PlayerData, hits).
  Breakable walls, gates, levers and scene events follow the game's own logic. Enemies keep the engine's behaviour.
- Walls that scripts open or close (breakable walls, battle gates) are out of the static collision grid and follow
  their object; `tools/merge_ssr.py` swaps in new grids without rebaking the scenery.
- PlayerData flags saved with the game (a wall broken stays broken).
- Testing: files in `SD:/3ds/3dsilksong/override/` replace the data file's; the autotest log lists FSM transitions.

## 0.8.0 — all of Hornet's moves, smooth streaming

- Scenery tiles are read and inflated on a loader thread (the New 3DS's spare core): walking through a room no
  longer stalls frames. Entering a room loads one more tile around the view and the objects' sprite pages.
- The abilities found later in the game, all unlocked: Cling Grip (cling to walls, slide, wall jump), Faydown
  Cloak (double jump), Drifter's Cloak (float while B is held), Clawline (L: pulled to a wall or an enemy ahead),
  Silk Soar (down + L held on the ground: a flight straight up).
- The air dash glides on ("Sprint Air"), longer while R is held; a sprint turns round with its skid; landing with
  R held goes on sprinting.
- Ledge grab: the hang and the vault play in full before a jump out of it.
- A: a bind with too little silk plays its failed version; binding works at full health; benches fill the spool.

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
