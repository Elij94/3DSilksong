# Formats of the converted data

Everything the tools write to `romfs/`, then into `3dsilksong.romfs` (`SD:/3ds/3dsilksong/`). Numbers are
little-endian. Strings are fixed-size, UTF-8, padded with `\0`. Units are the game's (1 unit = 64 px in the original
textures). `y` points up.
`tools/inspect_data.py` reads these files back and checks their structure; it doubles as a reference reader.

```
romfs/
  version.txt                 the version (source/version.h), written by the data step
  rooms/<Scene>.ssr           a room: collisions, gates, triggers, table of the scenery layers
  rooms/<Scene>.ssl           the room's scenery tiles
  rooms/<Scene>.sso           the room's live objects (enemies, breakables, NPCs, interactables...)
  rooms/<Scene>.spt           the room's particle emitters
  particles/textures.sss      the particle textures (one single-image clip per texture)
  ui/<atlas>.sss              the interface sprites of one atlas (HUD, inventory, map, menus...), one clip per sprite
  fonts/<font>.ssf            a font: glyph table and distance-field (SDF) atlas in A8
  sfx/index.txt               sound guid -> file, for the {"guid": ...} of FSM parameters
  fsm/<Scene>.json            the room's PlayMaker state machines, decoded
  fsm/templates/<guid>.json   the FSM templates that RunFSM actions start
  sprites/<name>.sss          a tk2d sprite set with its animations (the hero, and every library the objects use:
                              <name> = the library's name in lower case, with _)
  music/<cue>.ogg             music and ambience (Ogg Vorbis, 32 kHz, stereo)
  music/<cue>.pcm             the same for the engine: 16-bit signed little-endian, mono, 22050 Hz, no header
  music/rooms.txt             Scene<TAB>music<TAB>ambience, one line per room (empty: keep what is playing)
  sfx/<clip>.ogg              sounds (Ogg Vorbis, 32 kHz, mono)
  text/<language>/<Sheet>.txt the text
```

Readable JSON copies of each room and its objects are written to `work/rooms/`, outside the RomFS.

## Textures (`tools/ctrtex.py`)

They are already in GPU order: a `memcpy` into a `C3D_Tex` of the same size and format is enough, like `tex_upload`
in HK3DS.
- The image is first flipped vertically, since the GPU's texture origin is the bottom-left corner.
- It is then cut into 8x8 blocks, row by row, and the texels of each block are stored in Morton order.
- Formats: `0` = RGBA8 (`u32`: R<<24 | G<<16 | B<<8 | A), `4` = RGBA4 (`u16`: R<<12 | G<<8 | B<<4 | A) and `8` = A8
  (one byte per texel, for fonts): the `GPU_TEXCOLOR` values.

## Room: `.ssr` (`tools/build_room.py`)

| Field | Type |
|---|---|
| magic | `'SSRM'` |
| version | u16 (1) |
| cells_per_unit | u16 (4 by default) |
| grid_x, grid_y | f32: bottom-left corner of cell (0, 0), in units |
| grid_w, grid_h | u16 |
| spawn_x, spawn_y | f32: the first `HazardRespawnMarker`, else the first gate |
| n_gates, n_triggers, n_layers | u16 ×3 |
| (reserved) | u16 |

The header is 36 bytes. It is followed by:

**The collision grid.** `grid_h` rows **from the bottom up**, of `(grid_w + 7) / 8` bytes each. Bit `i` of byte `k` is
cell `x = 8k + i` (least significant bit first), and `1` = solid. Sources: the non-trigger colliders of layer 8
(Terrain). Closed `EdgeCollider2D` loops (the "Chunk" objects) are filled. The loops of one collider combine even-odd
(a hole stays empty).

**The gates** (`TransitionPoint`), 92 bytes each:
`char name[16]`, `char target_scene[32]`, `char entry_point[16]`, `f32 x, y, w, h` (the trigger rectangle, bottom-left
corner), `f32 entry_dx, entry_dy` (`entryOffset`), `u8 always_enter_left, always_enter_right`, `u8 pad[2]`.

**The triggers**, 68 bytes each:
`u8 kind`, `u8 pad[3]`, `f32 x, y, w, h`, `f32 extra[4]`, `char name[32]`, where `kind` is:
- `1`: respawn point (`HazardRespawnMarker`), an empty rectangle at its position;
- `2`: hazard (`DamageHero`), `extra` = damage, hazard type;
- `3`: camera lock (`CameraLockArea`), `extra` = `cameraXMin, cameraXMax, cameraYMin, cameraYMax`.

**The scenery layers**, 40 bytes each, each followed by its tile table:
`char name[8]`, `f32 z`, `f32 parallax`, `f32 ox, oy`, `f32 ppu`, `u16 tile`, `u16 ntx, nty`, `u16 n_tiles`,
`u8 fmt`, `u8 blend`, `u8 pad[2]`. Then, for each of the `n_tiles` tiles: `u16 tx, ty`, `u32 offset, size`, which
point into the `.ssl` at the data of one `tile`×`tile` tile, zlib-compressed.
- Names: `front`, `main`, `bg1`…`bg4` by depth, followed by `+` for an additive layer and `*` for a multiply layer.
- `blend`: `0` = normal alpha; `1` = additive (additive or "screen" materials: glows, light shafts), to draw with
  `GPU_SRC_ALPHA, GPU_ONE`; `2` = multiply (shadows, "darkness"), to draw with `GPU_DST_COLOR, GPU_ZERO`.
- `ppu` may be lower than the one asked for: a layer over 60 Mpx is drawn at a lower resolution.
- A layer keeps only what the camera can show from inside the room: the room ± half the view / `parallax`.
- Tile `(tx, ty)` covers, in world units, `x ∈ [ox + tx·tile/ppu, ox + (tx+1)·tile/ppu]` and
  `y ∈ [oy − (ty+1)·tile/ppu, oy − ty·tile/ppu]` (`ty` counts **down** from the top of the layer).
- Fully transparent tiles are not stored.
- **Parallax:** with the camera centred on `(cx, cy)` on the gameplay plane, a point `p` of the layer shows at
  `c + (p − c) · parallax` in gameplay-plane units. `parallax = d / (d + z)`, with `d = 38.1`. Layers with `z < 0` are
  in front of the player.
- **Drawing order:** `bg4` … `bg1`, `main`, (player and objects), `front`, each depth in the order its layers are
  stored (normal, `+`, `*`). Within a layer, the sprites were drawn in Unity's order: sorting layer, order in layer,
  then far to near.

## State machines for the engine: `.ssf` (`tools/build_fsm.py`)

`rooms/<Scene>.ssf`, from `fsm/<Scene>.json` and the scene's hierarchy (`work/rooms/<Scene>.tree.json`, written by
`build_room.py`). Little-endian. Header `'SSFM'`, `u16 version (2)`, then:

- strings: `u16 n`, then `n` × (`u8 length`, bytes). Names, events, clips, PlayerData keys; index 0 is `""`.
- nodes, every game object of the scene: `u16 n`, then `n` × 18 bytes: `u16 name`, `s16 parent` (node, -1),
  `s16 object` (index in the `.sso`, -1), `f32 x, y` (world), `u8 active` (its own flag), 3 bytes pad.
- FSMs: `u16 n`, then each: `s16 owner` (node), `u16 name`, `u16 start state`; `u16 nvars` × (`u16 name`, value);
  `u16 nglobal` × (`u16 event`, `u16 state`); `u16 nstates` × (`u16 name`, `u16 ntransitions` × (`u16 event`,
  `u16 state`), `u16 nactions` × (`u8 op`, `u8 everyFrame`, `u8 nparams`, values)). The ops and the order of their
  parameters are `OPS` in `build_fsm.py` (and `source/fsm_ops.h`); other actions are dropped.
- walls the FSMs control (version 2): `u16 n` × (`s16 node`, `f32 x0, y0, x1, y1`). These colliders (terrain
  colliders on objects with an FSM or a `Breakable`, or on their children, or inactive at load) are left out of the
  room's collision grid; the engine tests them while their node is active and its collider on.

Version 4 nodes are 42 bytes: after `active`, `s8 body` (Rigidbody2D: -1 none, 0 dynamic, 1 kinematic, 2 static),
2 bytes pad, `f32 x, y, w, h` (the union of its colliders, world), `f32 gravity scale`, `f32 scale x`.
Version 3 adds, after the walls, the enemies: `u16 n` × (`s16 object`, `u16 hit profile`, `u16 death profile`,
`u16 corpse sprite set`, `f32 corpse fling speed`, `u8 flags` (1 low arc, 2 corpse faces right, 4 no corpse, 8 freeze
instead of recoil, 16 has a Recoil), `f32 recoil speed`, `f32 recoil time`, `u8 n` × `u16` death sound guids), then
(version 5) `u8` + Walker (`f32` speed left, right, pauses, pause min, max, wait min, max, `u8` ignore holes, start
inactive, `f32` right scale, edge adjust), (version 6) `u8` + Crawler (`f32` speed, `u8` sprite faces right, `u16` clip,
`u8` start inactive), `u8` + WalkerV2 (`f32` speed, `u16` walk, turn, idle clips, `f32` wall distance, ground ahead,
`u8` start active). Version 6 ends with the objects' states: `u16 n` × (`s16 object`, `u8` persistent, `u8`
semi-persistent, `u16` id, `u8` PlayerData switch, `u8` off when true, `u16` PlayerData bool). Version 7 adds to each
enemy, after WalkerV2, its drops (`u16` small, medium, large, large smooth rosaries, shell shards, `u8` mega fling),
and to each state `u8` + a TestGameObjectActivator (`s16` node to activate, `s16` node to deactivate when the test
fails, `u8` groups × (`u8` tests × (`u8` type, `u16` PlayerData field, `u8` bool, `u8` number test, `s32`, `f32`))).

`sprites/rosary.sss` (`tools/build_items.py`): clips "Small|Med|Large Air|Idle|Land|Gleam" and "Shard" (one frame
per shell shard sprite). `sprites/hud_spool.sss`: the spool's rod, cap and bind notch, the counters' icons.

A value is `u8 tag` then: 0 none; 1 `f32`; 2 `s32`; 3 `u8` bool; 4 `u16` string; 5 `u16` variable; 6 `s16` node
(-2 the owner, -3 Hornet, -1 none); 7 `u16` event; 8 `f32 x, y, z`; 9 `u8 n` + n values; 10 event target: `u8 kind`
(0 self, 1 the object's FSMs, 2 one of its FSMs, 4 all), `s16 node`, `u16 fsm name`.

## Hit and death effects: `fx/` (`tools/build_fx.py`)

`fx/profiles.txt`: one profile per line, tab-separated: `H` (hit) or `D` (death), its name, `1` if a hit flashes the
enemy white, its effect prefabs (comma-separated), its sound guids (comma-separated), blood (min count, max count,
min speed, max speed). `fx/<prefab>.spt`: the prefab's particle systems in the `.spt` format, positions relative to the
prefab; the engine plays them once where a hit or a kill happens, turned to its direction.

## Objects: `.sso` (`tools/objects.py`, written by `build_room.py`)

Header: `'SSOB'`, `u16 version (1)`, `u16 n`. Then `n` objects of 244 bytes:

| Field | Type |
|---|---|
| flags | u16: `1` enemy (`HealthManager`), `2` breakable (`Breakable`), `4` interactable (NPC, bench, lever, door, chest, item…), `8` animated (tk2d), `16` has an FSM, `32` deals damage (`DamageHero`), `64` inactive at load |
| parent | s16: the nearest listed object among its ancestors, `-1` if none (the parts of one enemy) |
| x, y, z | f32: world position |
| scale_x, scale_y | f32: world scale (`scale_x < 0`: flipped) |
| rect | f32 ×4: `x, y, w, h`, the union of its colliders (else an empty rectangle at its position) |
| hp, damage | s32: hit points (`HealthManager.hp`), damage dealt (`DamageHero.damageDealt`) |
| sprite_id | u16: fixed tk2d sprite (`0xFFFF`: none) |
| (pad) | 2 bytes |
| name | `char[32]` |
| sprite_set | `char[32]`: the `sprites/<sprite_set>.sss` file of its animation |
| clip | `char[32]`: the animator's default clip |
| collection | `char[32]`: the tk2d collection of the fixed sprite |
| fsms | `char[64]`: the names of its FSMs, comma-separated |

## Particles: `.spt` (`tools/particles.py`, written by `build_room.py`)

Header: `'SSPT'`, `u16 version (1)`, `u16 n`. Then `n` emitters, each followed by `char texture[32]` (the clip of
`particles/textures.sss`) and `char name[32]`:

| Field | Type |
|---|---|
| flags | u16: `1` looping, `2` prewarmed, `4` plays on awake, `8` particles in world space, `16` inactive at load |
| blend | u8: as for scenery layers (`0` normal, `1` additive, `2` multiply) |
| shape | s8: Unity shape (`0` sphere, `4` cone, `5` box, `10` circle, `12` edge, `17` rectangle; `-1`: a point) |
| x, y, z, rotation | f32 ×4 (rotation in degrees) |
| duration, rate | f32: length of a cycle (s), particles per second |
| max_particles, burst_count | u16 ×2 |
| burst_time | f32 |
| life, speed, size, start_rotation | (f32 min, f32 max) ×4 |
| gravity, radius, angle | f32 ×3 |
| box | f32 ×2: the shape's scale |
| color0, color1 | f32 ×4 ×2: start colour, picked between the two |
| end_color | f32 ×4: colour at the end of life (if the Color module is on, else white) |
| end_size | f32: size multiplier at the end of life |
| tiles_x, tiles_y | u8 ×2: the texture is an animation sheet of `tiles_x × tiles_y` frames |
| (pad) | u16 |

Unity's curves are reduced to their range (min, max).

## Fonts: `.ssf` (`tools/build_font.py`)

Header: `'SSFN'`, `u16 version (1)`, `u16 n_glyphs`, `f32 point_size, line_height, ascender, descender, padding`,
`u16 atlas_w, atlas_h`, `u8 fmt (8 = A8)`, `u8 pad[3]`, `u32 compressed size`. Then `n_glyphs` glyphs:
`u32 code (Unicode)`, `f32 u0, v0, u1, v1`, `f32 x_offset, y_offset, width, height, x_advance` (in the font's points:
multiply by `wanted size / point_size`). Then the atlas data (A8, zlib).
The atlas is a distance field: draw it with an alpha test around 0.5, or a `smoothstep` for soft edges.

## FSM sounds: `sfx/index.txt` (`tools/build_music.py --fsm-sfx`)

One line per sound, `guid<TAB>file.ogg`. FSM action parameters that name a sound (`PlayAudioEvent`,
`AudioPlayerOneShotSingle`…) carry `{"guid": …}`: this index gives the matching file in `sfx/`.

## State machines: `fsm/<Scene>.json` (`tools/fsm_extract.py`, after HK3DS)

A list of FSMs. Each has `fsm` (its name), `owner` (the object's path in the scene), `start`, `events`, `global`
(global transitions), `variables` (`{name: {type, value}}`) and `states`. Each state has `name`, `transitions`
(`{event, to}`) and `actions` (`{type, enabled, params}`).
Parameter values:
- numbers or strings;
- `{"var": name}` for an FSM variable;
- `{"owner": true}` or `{"go": fileID}` for an object;
- `{"event": name}` for an event;
- `{"template", "name", "inputs"}` for a `RunFSM`, the template then being in `fsm/templates/<template>.json`.

FSMs built on an `FsmTemplate` are already merged: the template's states, the component's variables.

## Sprite set: `.sss` (`tools/build_sprites.py`)

| Part | Content |
|---|---|
| header (14 bytes) | `'SSSP'`, `u16 version (1)`, `u16 n_pages`, `u16 n_frames`, `u16 n_clips`, `u16 reserved` |
| pages (16 bytes ×n) | `u16 w, h`, `u8 fmt`, `u8 pad`, `u16 pad`, `u32 reserved`, `u32 compressed size` |
| frames (36 bytes ×n) | `u16 page`, `u16 pad`, `f32 u0, v0, u1, v1` (texture coordinates, `v` from the bottom), `f32 x0, y0, x1, y1` (the quad in units, relative to the sprite's origin) |
| clips | `char name[32]`, `f32 fps`, `u16 loop_start`, `u8 wrap` (tk2d mode: 0 loop, 1 loop section, 2 once…), `u8 pad`, `u16 n`, then `s16 frame[n]` (`-1` = frame not found) |
| data | the pages, zlib-compressed, in order |

The quad stays in game units whatever `--scale` is: only the texture resolution changes. Identical frames are stored
once.

## Text: `.txt` (`tools/build_text.py`)

One entry per line, `KEY<TAB>text`, in UTF-8. In the text, `\n` is a line break, `\f` a page break and `\\` a
backslash. The rest of the game's markup is removed.

## Music and sound (`tools/build_music.py`)

Ogg Vorbis, quality 3, 32 kHz. A music track is the mix of the chosen channels of its `MusicCue` / `AtmosCue` (by
default channel 0, the main layer). The file name is the cue's name in lower case, with `_`.
