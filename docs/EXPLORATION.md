# Étape 1 : exploration de Silksong

Relevé fait le 2026-11-23 sur la version Game Pass (`C:\XboxGames\Hollow Knight- Silksong`), directement depuis les
fichiers du jeu avec `tools/inventory.py`, puis par un export AssetRipper de test de la salle `Tut_01` (plus bas).
Résultat complet de l'inventaire : `work/inventory.json`.

## Ce qu'on sait déjà

| | Hollow Knight (HK3DS) | Silksong |
|---|---|---|
| Unity | 6000.0.61f1 | **6000.0.50f1** |
| Scripts | Mono | **Mono** (`Managed/Assembly-CSharp.dll`, 7 Mo) : décompilable, pas d'IL2CPP |
| Chiffrement | non | **non** (en-têtes `UnityFS` lisibles) |
| Rangement des données | `levelN` + `sharedassetsN.assets` | **Addressables** : 2068 bundles dans `StreamingAssets/aa/StandaloneWindows64` (7,7 Go) |
| Scènes | 480 | **590**, une par bundle (`scenes_scenes_scenes/*.bundle`), 61 préfixes de zone |
| Sprites | tk2d | **tk2d** (`TeamCherry.TK2D.dll`) + atlases Unity |
| Machines à états | PlayMaker | **PlayMaker** + 65 bundles `fsmtemplates` (FSM partagés entre objets) |

### Bundles par catégorie (les plus lourds)

| Catégorie | Nombre | Taille |
|---|---:|---:|
| scenes_scenes_scenes | 590 | 2,36 Go |
| tk2dcollections | 59 | 1,74 Go |
| atlases_assets_assets | 258 | 1,53 Go |
| sfxstatic | 230 | 576 Mo |
| textures | 143 | 477 Mo |
| cinematics | 26 | 225 Mo |
| audiocuesstatic | 36 | 225 Mo |
| **herocollections** | 9 | 130 Mo (les sprites de Hornet) |

### Zones (nombre de salles)

Bone_East 40, Coral 31, Song 30, Under 30, Shadow 28, Bone 25, Greymoor 25, Shellwood 24, Slab 23, Dock 19, Hang 19,
Library 19, Peak 17, Ant 16, Clover 15, Abyss 14, Bellway 14, Cog 13, Dust 13, Arborium 12, Dust_Maze 12, Aqueduct 11,
Crawl 11, Weave 11, Ward 10, Belltown 9, Wisp 8, Tut 6… La liste complète des 590 scènes est dans
`tools/data/all_rooms.txt`.

### Scripts : les systèmes à réimplémenter

Noms relevés dans les métadonnées d'`Assembly-CSharp.dll` (lecture seule) :
- **Héroïne** : ~127 types `Hero*` (`HeroController` et ses boîtes de collision : `HeroBoxAirdash`, `HeroBoxDownspike`,
  `HeroBoxHarpoon`…).
- **Soie** : ~90 noms (`AddSilk`, `DoSilkRegen`, `AddToMaxSilk`…), l'équivalent de l'âme de HK.
- **Outils et crêtes** : `ToolItemBasic`, `ToolCrestsData`, `ToolBoomerang`… ; ~90 noms autour des crêtes
  (remplacent les charmes).
- **Transitions** : `TransitionPoint`, `BeginSceneTransition`… (même logique de portes que HK).
- **Boss** : `BossSceneController`, `BossHealthDetails`…

## Ce qui change par rapport à HK3DS

1. **Addressables.** Une scène ne contient presque plus ses propres ressources : elle pointe vers des bundles
   partagés (tk2d, atlases, sons). AssetRipper sait les charger ensemble si on lui donne tout le dossier `_Data`,
   mais l'export sera plus gros et plus long que celui de HK. Les outils doivent résoudre les guid à travers tout
   l'export (d'où les recherches récursives dans `index_export.py`).
2. **FSM templates.** Beaucoup d'ennemis utilisent un `FsmTemplate` partagé au lieu d'un FSM écrit dans la scène.
   `dump_fsms.py` / `fsm_extract.py` de HK3DS devront suivre ce lien.
3. **Hornet ≠ le Chevalier.** Ses sprites viennent des 9 bundles `herocollections`, et son jeu de mouvements est
   plus riche (sprint, harpon, attaques en diagonale vers le bas selon la crête). Le moteur héros sera neuf.
4. **Taille des données.** HK3DS HD fait ~2 Go. Silksong a plus d'art (3,3 Go de tk2d + atlases contre ~1,5 Go pour
   HK) : il faudra surveiller la limite de **4 Go par fichier** des cartes SD en FAT32, et peut-être découper le RomFS
   en plusieurs fichiers (par exemple un par groupe de zones).

## Export AssetRipper : le piège de la version

Les bundles Addressables de Silksong ont leur version Unity **effacée** (`0.0.0` dans l'en-tête). Avec les réglages
par défaut, AssetRipper charge les 2068 bundles mais n'arrive à lire **aucun objet** dedans (tout sort en
`Unreadable*`, même les `PolygonCollider2D`) : l'export projet ne contient alors que le jeu de base (514 objets,
une seule scène, `Pre_Menu_Loader`) et une copie brute des bundles.

**Solution :** régler `DefaultVersion = 6000.0.50f1` (formulaire `/Settings/Update`). `tools/export_game.sh` le fait
avant de charger le jeu. Il en va de même pour UnityPy (`UnityPy.config.FALLBACK_UNITY_VERSION`, dans `bundle_deps.py`).

Autres points notés :
- les endpoints headless attendent un formulaire **URL-encodé** (`curl --data-urlencode`) ; en multipart, réponse 415 ;
- chaque requête rend la main quand le travail est fini (l'export projet puis le Primary Content, l'un après l'autre).

### Export de test : Tut_01

`tools/bundle_deps.py` suit les liens CAB entre bundles. La fermeture complète de `Tut_01` touche 535 bundles
(3,3 Go : les bundles partagés pointent vers presque tout le jeu) ; ses **dépendances directes** ne sont que
**72 bundles, 386 Mo**. Avec `--depth 1 --subset`, on obtient un `_Data` réduit qui s'exporte en ~2 min sans charge
mémoire notable.

Résultat : `ex/Scenes/Hornet/Tut_01.unity` (21 Mo de YAML, 12 140 objets) :

| Type | Nombre |
|---|---:|
| GameObject / Transform | 3552 |
| SpriteRenderer | 2694 (tous pointent vers un sprite par guid) |
| MonoBehaviour | 1174 |
| BoxCollider2D | 372 |
| ParticleSystem | 197 |
| PolygonCollider2D / EdgeCollider2D | 32 / 28 |

- **Collisions du décor :** calque 8 (« Terrain », comme dans HK) : 15 `PolygonCollider2D` (4 à 21 points),
  28 `EdgeCollider2D` et 9 `BoxCollider2D`. C'est la même logique que les salles de HK, donc `build_room.py` est un
  bon point de départ.
- **Sorties :** 6 `TransitionPoint` (`left2`, `right2`, `top1`…), le même composant que dans HK.
- **Comportements :** 141 composants tk2d (`TeamCherry.TK2D.dll`), 73 `PlayMakerFSM`, `CameraLockArea` (18),
  `HazardRespawnMarker`, `Breakable`, `GrassCut`, `MossClump`, `DamageHero`…
- Les scripts des DLL (PlayMaker, tk2d, SharedUtils) se reconnaissent au guid du `.dll.meta` dans `ex/Plugins`,
  et le type précis au `fileID` du `m_Script`.

## Outils HK3DS réutilisables

| Outil HK3DS | Pour 3DSilksong |
|---|---|
| `hkcommon.py` | repris dans `sscommon.py` (chemins `SSDATA`, lecture YAML Unity) |
| `index_export.py` | repris, recherches récursives + index des scènes |
| `build_room.py`, `lyr_*.py`, `masks.py` | à adapter à l'étape 2 (collisions `PolygonCollider2D` / `EdgeCollider2D`, couches de décor) |
| `dump_fsms.py`, `fsm_extract.py` | à adapter : gestion des `FsmTemplate` |
| `build_music.py`, `ctrfile.py`, `mkromfs.py` | probablement réutilisables tels quels |
| `build_knight.py` | à réécrire pour Hornet |

## Prochaines actions

1. **Export complet** (`tools/export_game.sh`, avec `DefaultVersion`) quand la machine a 16 à 24 Go de RAM libres.
   En attendant, le sous-ensemble Tut_01 suffit pour commencer l'étape 2.
2. ~~Convertisseurs de salles, sprites, musique et textes~~ : faits (`build_room.py`, `build_sprites.py`,
   `build_music.py`, `build_text.py`), testés sur `Tut_01`. Les formats sont dans [FORMATS.md](FORMATS.md).
3. Une fois l'export complet fait : lancer `tools/build.sh` sur les 590 salles et surveiller la taille du RomFS
   (limite de 4 Go par fichier en FAT32).
4. Restent à convertir : les objets des salles (ennemis, PNJ, objets cassables) et les FSM PlayMaker (`FsmTemplate`).
   Les matériaux additifs du décor sont pour l'instant dessinés en alpha normal.
