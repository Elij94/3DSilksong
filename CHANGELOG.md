# Changelog

## 0.20.0 — items on the ground, tools and crests, the boss arenas complete

- Items lying in the rooms (CollectableItemPickup: 129 in 106 rooms, tools/build_pickups.py): drawn, picked up when
  Hornet touches them, counted in the save (item:<name>, tool:<name>, the maps' and keys' PlayerData), never there
  again, their name and icon shown a moment.
- Tools and crests (tools/build_tools.py: 66 tools, 11 crests): a tool found or bought goes in a free slot of its
  colour on her crest; red tools are thrown with R (up, neutral or down: the slot's binding), their stock taken and
  refilled at benches, shown on the HUD; a Tools tab in the inventory (L, R) shows the crest, the tool in each slot
  and its description, changed with A at a bench. "Every tool" in the Tests menu.
- What hurts enemies (DamageEnemies: the thrown tools...) hits them.
- Additive scenes: their scenery layers and particles too (the Bell Beast hanging in its silk in Bone_05).
- Object tags (FindClosest, FindGameObject by tag, GetTag, GameObjectCompareTag): the digging pilgrims come up on
  their Teleplanes.
- SetIsKinematic2d(false) makes a kinematic body dynamic (Unity's); GetDefaultTk2dClipName (sleeping and ambushing
  enemies start as the game's); about 35 more actions (silk, switches, map and scene checks, eased moves, tools
  equipped, damage taken...): 964 000 of the rooms' actions run.
- An emulator test folder (override) left from a test is no longer in the SD folder.

## 0.19.0 — enemies everywhere, the title screen as the game's, the first bosses

- PlayMaker's global "Hero" variable (45 500 uses in the rooms' scripts) now points at Hornet: enemies face her, chase
  her, leap and swoop at her instead of at the room's corner.
- Greymoor_12: the Mitefly's sprite took its local position for a world one and crossed the room; the Mites stood
  still (WalkLeftRight unknown). Both as the game's now.
- About 60 more script actions: WalkLeftRight, DistanceFlyV2, Check X/Y Position, CheckPassedTarget, ChaseObjectVertical,
  IdleBuzzV2, FaceObjectV3, Decelerate, AddForce2d, ShoveFromWall, ClampSpeed, ClampVelocity2D, MatchScaleSign,
  TranslateRandom, ObjectJitter, SetRecoilBlocked, CompareHP, MultiplyIntByFloat, SendEventByNameV3, SetHP/GetHP/AddHP/
  SubtractHP, InstaDeath, TimeLimitSet/Check, random and float/int/vector helpers... (906 000 → 917 000 kept).
- tk2d animation trigger frames: the events scripts wait for on an attack's frame now come (Mitefly attacks).
- Crawler as the game's: along its floor or roof, turning (with its turn clip) at a wall ahead or where the ground
  ends; no more crawlers flipping on the spot.
- Bodies: about 20 000 enemies had no collider (tk2d gives them a box from their sprite at run time): they get it now
  (the Crowman no longer falls out of Greymoor_15b). Bodies slide up small slopes and seams as Unity's do.
- SetPosition2D with a vector ignores its X and Y (mosquitoes, Bell Goombas no longer sent to 0,0); iTween moves by
  speed; GetPosition2dAs3d (the dragonflies' route).
- Bosses: Moss Mother's swoop no longer runs away out of the arena (KeepFloatPositive); phases by HP (CompareHP); the
  boss's name shown at the start of its fight as the game does (Titles.txt).
- Scenes loaded on top of a room (SceneAdditiveLoadConditional: boss arenas, the Bellway beast, cutscenes; 35 of them,
  text/additive.txt from tools/build_additive.py), when their PlayerData tests pass: the Bell Beast hangs in Bone_05,
  freed by cutting its vines, then fights (submerge, emerge, charge). Their scenery and particles are not yet drawn.
- Title screen measured on the game's: framed as its 16:9 screen (the logo centred, the needle three quarters across,
  stuck in the bone pile on the bottom edge), its colour grading (red, darker sides and top, the lava's red glow),
  the logo kept white above it, and its embers: particle boxes are scaled by their object as in Unity (every room's
  ambient particles spread as wide as the game's), up to 150 live particles an emitter.

## 0.18.0 — the game's room entries, its down spike, its menus; the scenery cleaned up

- Room entries as HeroController.EnterScene: walked in from the sides (0.33 s at run speed), dropped in from above,
  thrown up from below (3 units over the gate, then 9.4 up and 8 across, falling on across until she lands: no more
  falling back into the room she left), out of doors with Exit Door To Idle.
- The down spike plays the Hunter crest's DownSlashEffect and its Downspike Burst, at their places by her.
- Scenery: a sprite is drawn only within its mesh, as Unity does; the tightly packed atlases put other sprites inside
  a sprite's rectangle, and those stray bits showed in most rooms (74 of Tut_01's 123 sprites). Items to pick up and
  scripted critters (silk flies) are no longer baked as still pictures. Every room re-baked.
- Title screen: the camera on the logo and framed as the game's (centred, the logo its size); the selection's fleurs
  are the game's pointers; the port's version bottom left.
- Profile screen as Silksong's: 4 cards side by side, each with its area's picture, masks, needle, rosaries, shards,
  play time and area name, the fleurs and the selection arrows. Play time is counted (PlayerData playTime).
- Tests: `warp <Scene>@<gate>` in autotest.txt enters a room by a gate.

## 0.17.0 — from the console test: scenery streaming, lights, menus, swimming, a test menu

- Scenery streaming: a room's tile file is read into memory when it is entered (no card reads while walking), the
  tiles the view shows are decoded before the ones asked ahead, further ahead the way the camera goes, by two loader
  threads (the New 3DS's spare core and part of the system core), and the game thread decodes a visible tile still
  waiting after two frames. In the emulator's test run, missing tiles went from 445 to 46.
- Red halos and black rectangles: the "Vignette Cutout" sprites (Sprites-Darkness-Cutout) only cut holes in the
  game's darkness vignette, their colour is a mask; they were baked as pictures. Left out now.
- Scenery shown twice: the variants a new game starts without (DeactivateIfPlayerdataFalse, the side of a
  TestGameObjectActivator its test leaves off: an opened door, the Act 3 threads...) are left out of the bake.
- Menus: thin strokes of small letters vanished (an E's bars): small text keeps a lower distance-field contour.
- Swimming (HeroWaterController): water regions (SurfaceWaterRegion) are converted; Hornet floats at the top, swims
  (faster with ZR), jumps out, with the game's Surface animations and sounds. Greymoor's pools no longer drop her
  out of the room.
- Dead enemies' attack hitboxes and damage colliders no longer hurt.
- Inventory: the selection's name and description on the top screen, in large letters; 6 rows of items below.
- A "Tests" menu in the pause menu: abilities on and off one by one, +1000 rosaries, +100 shell shards, every map
  and the quill, health and silk full, one more mask (PlayerData maxHealth, kept with the save).
- Tests: `<t> shot <name>` in autotest.txt saves the top screen as shown (800 or 400 wide) to the SD card.

## 0.16.0 — the inventory on the bottom screen

- SELECT opens the inventory's Inv pane on the bottom screen while the game waits (tools/build_inventory.py,
  source/inventory.c): the needle at its level, the silk spool, the heart and spool pieces, the skills Hornet has,
  the rosaries and shell shards, the collectables she holds (the game's 87, their states and counts), and the
  selection's name and description, in the game's text. The pad moves the selection; SELECT or B closes it. The room
  list (debug) is in the pause menu only.

## 0.15.0 — the game's map on the bottom screen, shops

- The map (tools/build_map.py, from the game's Game_Map_Hornet prefab): once an area's map is bought, the bottom
  screen shows it, each room's rough outline, the rooms visited drawn in full once mapped (a bench rest with the
  quill, as GameMap does); rooms map their parts as Hornet goes through them (MapSceneRegion, SetMappedOnStart).
- Shops (tools/build_shops.py, source/shop.c): the stocks of the game's ShopOwners (31 merchants, 83 items), shown
  over the top screen as the Shop Menu does: the items still for sale with their icons and prices, their names and
  descriptions, the purchase confirmed; a purchase takes the rosaries or shards and sets the item's PlayerData (maps,
  the quill, pins...). The scripts' GetShopObject / ShopObject, ShopHasStock, ShopWasItemPurchased, SHOP CLOSED and
  SHOP NO STOCK; PlayerDataVariableTest, HasAnyMap, CanUpdateMap.
- Scripts' events reach the right FSMs: the compiler took an event target for a plain object reference, so events
  meant for other objects or for every FSM (broadcasts) went back to the sender; and those sent to an object held in
  a variable (22,671 SendEventByName actions) reached nobody.
- RunFSM (1,038 in the game): the template FSMs a script runs (fsm/templates) are compiled as sub-FSMs of the same
  object (format 9), started with their inputs, finished by FinishFSM, stopped when their host leaves the state;
  events to the host or the sub-FSM. CheckQuestState (no quest taken yet), CheckTrackTriggerCount, LINE_END between
  a dialogue's lines. Shakra (the map seller) now places herself, talks, opens her shop.
- The game's text is cached as it is asked for: the menus read a whole language file each frame (the shop fell to
  5 fps).
- The menus' own sounds were never converted (the engine plays them by name): tools/build_sfx_missing.py now converts
  every sound the engine names. tools/build.sh gets its missing steps (prefabs, shops, sfx, zones).

## 0.14.0 — projectiles, effects and summons; script-driven particles; enemy sounds

- Spawned prefabs, as the game's global pool: tools/build_prefabs.py converts the 242 prefabs the scripts spawn
  (projectiles, effects, summoned enemies) into packs the engine puts in the room with their objects, FSMs and
  particles, and recycles. SpawnObjectFromGlobalPool (+Delay), CreateObject, FlingObjectsFromGlobalPool, RecycleSelf,
  RecycleObject; projectiles hit walls (Trigger2dEventLayer on the terrain) and Hornet.
- Particles the scripts drive: every emitter kept and tied to its node (format 8); PlayParticleEmitter,
  StopParticleEmitter, SetParticleEmission, PlayParticleEmitterInState; emitters follow their objects and stop with them.
- Enemy sounds and voices from the game's sound tables (PlayRandomAudioClipTable, AudioPlayRandomVoiceFromTable),
  3,160 more sounds converted (tools/build_sfx_missing.py).
- Objects turn (SetRotation, Rotate, FaceAngle: projectiles point where they fly); EaseFloat.
- Hurting colliders follow their shape (a long spike strip no longer hurts on the floor beside it); objects switched
  off in the scene get their sprites (enemies a script switches on were invisible).

## 0.13.0 — sharp picture, the game's framing, title screen and saves, real progression starts

- The 800-pixel top screen works (citro2d's projection), and the scenery and sprites are converted for it: 28 pixels
  a unit across, 14 down, shrunk with Lanczos; opaque tiles in RGB565, the rest RGBA4 dithered. Data 2.93 -> 2.30 GB.
- The camera frames like the game (8.71 units above and below the centre).
- The game's fonts (distance fields), the title screen from its Menu_Title scene, the menus in French, 4 save slots
  (the old save becomes slot 1), options (music, sound volumes, language, debug info), the pause menu.
- The bottom screen: the area's name (text/zones.txt); the debug info only with the option.
- Abilities need their PlayerData flags as in the game (SD:/3ds/3dsilksong/allabilities gives them all, for tests);
  buttons as the game's defaults (ZR dash, ZL Clawline, X Needolin).
- Scripts can take Hornet over (control, animations, facing, health), fade the screen, listen to buttons, broadcast
  events, tween objects; the Needolin, heard by the scripts in the game's regions.

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
