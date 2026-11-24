# Exploring Silksong's files

Notes taken on 2026-11-23 on the Game Pass version (`C:\XboxGames\Hollow Knight- Silksong`), first straight from the
game's files with `tools/inventory.py`, then from AssetRipper exports (a test export of the room `Tut_01`, then the
full game). The full inventory is written to `work/inventory.json`.

## What the game is made of

| | Hollow Knight (HK3DS) | Silksong |
|---|---|---|
| Unity | 6000.0.61f1 | **6000.0.50f1** |
| Scripts | Mono | **Mono** (`Managed/Assembly-CSharp.dll`, 7 MB): decompilable, no IL2CPP |
| Encryption | no | **no** (readable `UnityFS` headers) |
| Data layout | `levelN` + `sharedassetsN.assets` | **Addressables**: 2068 bundles in `StreamingAssets/aa/StandaloneWindows64` (7.7 GB) |
| Scenes | 480 | **590**, one per bundle (`scenes_scenes_scenes/*.bundle`), 61 zone prefixes |
| Sprites | tk2d | **tk2d** (`TeamCherry.TK2D.dll`) + Unity sprite atlases |
| State machines | PlayMaker | **PlayMaker** + 65 `fsmtemplates` bundles (FSMs shared between objects) |

### Bundles by kind (the heaviest)

| Kind | Count | Size |
|---|---:|---:|
| scenes_scenes_scenes | 590 | 2.36 GB |
| tk2dcollections | 59 | 1.74 GB |
| atlases_assets_assets | 258 | 1.53 GB |
| sfxstatic | 230 | 576 MB |
| textures | 143 | 477 MB |
| cinematics | 26 | 225 MB |
| audiocuesstatic | 36 | 225 MB |
| herocollections | 9 | 130 MB (the hero's sprites) |

### Zones (number of rooms)

Bone_East 40, Coral 31, Song 30, Under 30, Shadow 28, Bone 25, Greymoor 25, Shellwood 24, Slab 23, Dock 19, Hang 19,
Library 19, Peak 17, Ant 16, Clover 15, Abyss 14, Bellway 14, Cog 13, Dust 13, Arborium 12, Dust_Maze 12, Aqueduct 11,
Crawl 11, Weave 11, Ward 10, Belltown 9, Wisp 8, Tut 6… The full list of the 590 scenes is in
`tools/data/all_rooms.txt`.

## What differs from Hollow Knight

1. **Addressables.** A scene holds almost none of its own assets: it points into shared bundles (tk2d, atlases,
   sounds). AssetRipper loads them together when given the whole `_Data` folder, but the export is bigger and slower
   than Hollow Knight's. The tools resolve guids across the whole export (hence the recursive searches of
   `index_export.py` and the guid index of `sscommon.py`).
2. **FSM templates.** Many objects use a shared `FsmTemplate` instead of an FSM written in the scene, and `RunFSM`
   actions start templates as sub-machines. `fsm_extract.py` follows both.
3. **Size of the data.** Silksong has more art than Hollow Knight (3.3 GB of tk2d + atlases against ~1.5 GB), so the
   **4 GB per-file** limit of FAT32 SD cards matters. The full conversion stays under it (2.25 GB).

## AssetRipper export: the version trap

Silksong's Addressables bundles have their Unity version **stripped** (`0.0.0` in the header). With the default
settings, AssetRipper loads the 2068 bundles but can't read **a single object** in them (everything comes out as
`Unreadable*`, even `PolygonCollider2D`): the project export then holds only the base game (514 objects, one scene,
`Pre_Menu_Loader`) and a raw copy of the bundles.

**Fix:** set `DefaultVersion = 6000.0.50f1` (the `/Settings/Update` form). `tools/export_game.sh` does it before
loading the game. UnityPy needs the same (`UnityPy.config.FALLBACK_UNITY_VERSION`, in `bundle_deps.py`).

Other notes:
- the headless endpoints take a **URL-encoded** form (`curl --data-urlencode`); multipart gets a 415;
- each request returns when the job is done (the project export, then the primary content, one after the other);
- the converters only read the project export: the primary content export (4.3 million files) is optional
  (`PRIMARY=1`).

### Test export: Tut_01

`tools/bundle_deps.py` follows the CAB links between bundles. The full closure of `Tut_01` reaches 535 bundles
(3.3 GB: the shared bundles point at almost the whole game); its **direct dependencies** are only **72 bundles,
386 MB**. With `--depth 1 --subset`, that makes a small `_Data` that exports in ~2 min with little memory.

Result: `ex/Scenes/Hornet/Tut_01.unity` (21 MB of YAML, 12,140 objects):

| Type | Count |
|---|---:|
| GameObject / Transform | 3552 |
| SpriteRenderer | 2694 (all point at a sprite by guid) |
| MonoBehaviour | 1174 |
| BoxCollider2D | 372 |
| ParticleSystem | 197 |
| PolygonCollider2D / EdgeCollider2D | 32 / 28 |

- **Terrain collisions:** layer 8 ("Terrain", as in Hollow Knight): 15 `PolygonCollider2D` (4 to 21 points),
  28 `EdgeCollider2D` (closed loops: the "Chunk" objects) and 9 `BoxCollider2D`.
- **Gates:** 6 `TransitionPoint` (`left2`, `right2`, `top1`…), the same component as in Hollow Knight.
- **Behaviours:** 141 tk2d components (`TeamCherry.TK2D.dll`), 73 `PlayMakerFSM`, `CameraLockArea` (18),
  `HazardRespawnMarker`, `Breakable`, `GrassCut`, `MossClump`, `DamageHero`…
- The DLL scripts (PlayMaker, tk2d, SharedUtils) are recognised by the guid of their `.dll.meta` in `ex/Plugins`, and
  the exact type by the `fileID` of `m_Script`.

## HK3DS tools reused

| HK3DS tool | In 3DSilksong |
|---|---|
| `hkcommon.py` | `sscommon.py` (`SSDATA` paths, Unity YAML reading, guid index) |
| `index_export.py` | reused, recursive searches + scene index |
| `fsm_extract.py` | reused, with `FsmTemplate` / `RunFSM` support and fault tolerance |
| `lyr_split.py` (Morton order) | `ctrtex.py` |
| `text_sheets.py` (decryption) | `build_text.py` |
| `build_room.py`, `build_music.py` | rewritten for Silksong's scenes and audio cues |

## Full conversion

Full export: 591 scenes in ~15 min, a single unreadable object in the whole game. Full conversion with
`tools/build.sh`: 590 rooms (575 with particles), 17,160 FSMs (2 unreadable), 397 sprite sets, 219 particle
textures, 56 interface atlases, 11 fonts, 2071 sounds, 109 music tracks, a 2.25 GB RomFS.
