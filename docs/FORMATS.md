# Formats des données converties

Tout ce que les outils écrivent dans `romfs/`, puis dans `3dsilksong.romfs` (`SD:/3ds/3dsilksong/`). Les nombres sont
en little-endian. Les chaînes sont de taille fixe, en UTF-8, complétées par des `\0`. Les unités sont celles du
jeu (1 unité = 64 px dans les textures d'origine). `y` monte vers le haut.
`tools/inspect_data.py` relit ces fichiers et vérifie leur structure. Il sert aussi de lecteur de référence.

```
romfs/
  version.txt                 la version (source/version.h), écrite par l'étape data
  rooms/<Scène>.ssr           une salle : collisions, sorties, déclencheurs, table des couches de décor
  rooms/<Scène>.ssl           les tuiles de décor de la salle
  rooms/<Scène>.sso           les objets vivants de la salle (ennemis, objets cassables, PNJ, interactifs…)
  fsm/<Scène>.json            les machines à états PlayMaker de la salle, décodées
  fsm/templates/<guid>.json   les modèles de FSM lancés par les actions RunFSM
  sprites/<nom>.sss           un jeu de sprites tk2d avec ses animations (le héros, et chaque bibliothèque
                              qu'utilisent les objets : <nom> = nom de la bibliothèque en minuscules, avec des _)
  music/<cue>.ogg             musiques et ambiances (Ogg Vorbis, 32 kHz, stéréo)
  sfx/<clip>.ogg              sons (Ogg Vorbis, 32 kHz, mono)
  text/<langue>/<Feuille>.txt les textes
```

Des copies lisibles en JSON de chaque salle et de ses objets sont écrites dans `work/rooms/`, hors du RomFS.

## Textures (`tools/ctrtex.py`)

Elles sont déjà dans l'ordre du GPU : il suffit d'un `memcpy` dans un `C3D_Tex` de la même taille et du même
format, comme `tex_upload` dans HK3DS.
- L'image est d'abord retournée verticalement, car l'origine des textures du GPU est en bas à gauche.
- Elle est ensuite découpée en blocs de 8x8, ligne par ligne, et les texels de chaque bloc sont rangés en ordre de Morton.
- Formats : `0` = RGBA8 (`u32` : R<<24 | G<<16 | B<<8 | A) et `4` = RGBA4 (`u16` : R<<12 | G<<8 | B<<4 | A), soit les
  valeurs de `GPU_TEXCOLOR`.

## Salle : `.ssr` (`tools/build_room.py`)

| Champ | Type |
|---|---|
| magic | `'SSRM'` |
| version | u16 (1) |
| cells_per_unit | u16 (4 par défaut) |
| grid_x, grid_y | f32 : coin bas-gauche de la cellule (0, 0), en unités |
| grid_w, grid_h | u16 |
| spawn_x, spawn_y | f32 : premier `HazardRespawnMarker`, sinon la première sortie |
| n_gates, n_triggers, n_layers | u16 ×3 |
| (réservé) | u16 |

L'en-tête fait 36 octets. Il est suivi de :

**La grille de collision.** Elle fait `grid_h` lignes **de bas en haut**, de `(grid_w + 7) / 8` octets chacune. Le bit `i` de
l'octet `k` correspond à la cellule `x = 8k + i` (bit de poids faible en premier), et `1` = solide. Sources : les
collisions non-déclencheurs du calque 8 (Terrain). Les boucles d'`EdgeCollider2D` fermées (les objets « Chunk ») sont
remplies. Les boucles d'un même collider se combinent en pair-impair (un trou reste vide).

**Les sorties** (`TransitionPoint`), 92 octets chacune :
`char name[16]`, `char target_scene[32]`, `char entry_point[16]`, `f32 x, y, w, h` (rectangle du déclencheur, coin
bas-gauche), `f32 entry_dx, entry_dy` (`entryOffset`), `u8 always_enter_left, always_enter_right`, `u8 pad[2]`.

**Les déclencheurs**, 68 octets chacun :
`u8 kind`, `u8 pad[3]`, `f32 x, y, w, h`, `f32 extra[4]`, `char name[32]`, avec pour `kind` :
- `1` : point de réapparition (`HazardRespawnMarker`), un rectangle vide à la position ;
- `2` : zone de dégâts (`DamageHero`), `extra` = dégâts, type de danger ;
- `3` : verrou de caméra (`CameraLockArea`), `extra` = `cameraXMin, cameraXMax, cameraYMin, cameraYMax`.

**Les couches de décor**, 40 octets chacune, suivies de leur table de tuiles :
`char name[8]`, `f32 z`, `f32 parallax`, `f32 ox, oy`, `f32 ppu`, `u16 tile`, `u16 ntx, nty`, `u16 n_tiles`,
`u8 fmt`, `u8 blend`, `u8 pad[2]`. Puis, pour chacune des `n_tiles` tuiles : `u16 tx, ty`, `u32 offset, size`, qui
pointent dans le `.ssl` vers les données d'une tuile `tile`×`tile` compressées en zlib.
- Noms : `front`, `main`, `bg1`…`bg4` selon la profondeur, suivis de `+` pour une couche additive et de `*` pour une
  couche multiplicative.
- `blend` : `0` = alpha normal ; `1` = additif (matériaux additifs ou « screen » : lueurs, rayons de lumière), à dessiner
  avec `GPU_SRC_ALPHA, GPU_ONE` ; `2` = multiplication (ombres, « darkness »), à dessiner avec `GPU_DST_COLOR, GPU_ZERO`.
- `ppu` peut être plus petit que celui demandé : une couche de plus de 60 Mpx est dessinée en plus basse résolution.
- Une couche ne garde que ce que la caméra peut montrer depuis la salle : la salle ± la moitié de la vue / `parallax`.
- La tuile `(tx, ty)` couvre en monde `x ∈ [ox + tx·tile/ppu, ox + (tx+1)·tile/ppu]` et
  `y ∈ [oy − (ty+1)·tile/ppu, oy − ty·tile/ppu]` (`ty` compte **vers le bas** depuis le haut de la couche).
- Les tuiles entièrement transparentes ne sont pas stockées.
- **Parallaxe :** pour une caméra centrée en `(cx, cy)` sur le plan de jeu, un point `p` de la couche s'affiche à
  `c + (p − c) · parallax` en unités du plan de jeu. `parallax = d / (d + z)`, avec `d = 38,1`. Les couches `z < 0` sont
  devant le joueur.
- **Ordre de dessin :** `bg4` … `bg1`, `main`, (joueur et objets), `front`, chaque profondeur dans l'ordre où les
  couches sont rangées (normale, `+`, `*`). Dans une couche, les sprites ont été dessinés dans l'ordre de Unity :
  calque de tri, ordre dans le calque, puis du plus loin au plus proche.

## Objets : `.sso` (`tools/objects.py`, écrit par `build_room.py`)

En-tête : `'SSOB'`, `u16 version (1)`, `u16 n`. Puis `n` objets de 244 octets :

| Champ | Type |
|---|---|
| flags | u16 : `1` ennemi (`HealthManager`), `2` cassable (`Breakable`), `4` interactif (PNJ, banc, levier, porte, coffre, objet…), `8` animé (tk2d), `16` a une FSM, `32` fait des dégâts (`DamageHero`), `64` inactif au chargement |
| parent | s16 : l'objet listé le plus proche parmi ses ancêtres, `-1` sinon (les morceaux d'un même ennemi) |
| x, y, z | f32 : position en monde |
| scale_x, scale_y | f32 : échelle en monde (`scale_x < 0` : retourné) |
| rect | f32 ×4 : `x, y, w, h`, union de ses colliders (sinon un rectangle vide à sa position) |
| hp, damage | s32 : points de vie (`HealthManager.hp`), dégâts infligés (`DamageHero.damageDealt`) |
| sprite_id | u16 : sprite tk2d fixe (`0xFFFF` : aucun) |
| (pad) | 2 octets |
| name | `char[32]` |
| sprite_set | `char[32]` : le fichier `sprites/<sprite_set>.sss` de son animation |
| clip | `char[32]` : le clip par défaut de l'animateur |
| collection | `char[32]` : la collection tk2d du sprite fixe |
| fsms | `char[64]` : les noms de ses FSM, séparés par des virgules |

## Machines à états : `fsm/<Scène>.json` (`tools/fsm_extract.py`, d'après HK3DS)

Une liste de FSM. Chacune contient `fsm` (son nom), `owner` (le chemin de l'objet dans la scène), `start`, `events`,
`global` (transitions globales), `variables` (`{nom: {type, value}}`) et `states`. Chaque état a `name`, `transitions`
(`{event, to}`) et `actions` (`{type, enabled, params}`).
Valeurs des paramètres :
- des nombres ou des chaînes ;
- `{"var": nom}` pour une variable de la FSM ;
- `{"owner": true}` ou `{"go": fileID}` pour un objet ;
- `{"event": nom}` pour un événement ;
- `{"template", "name", "inputs"}` pour un `RunFSM`, le modèle étant alors dans `fsm/templates/<template>.json`.

Les FSM construites sur un `FsmTemplate` sont déjà fusionnées : états du modèle, variables du composant.

## Jeu de sprites : `.sss` (`tools/build_sprites.py`)

| Partie | Contenu |
|---|---|
| en-tête (14 octets) | `'SSSP'`, `u16 version (1)`, `u16 n_pages`, `u16 n_frames`, `u16 n_clips`, `u16 réservé` |
| pages (16 octets ×n) | `u16 w, h`, `u8 fmt`, `u8 pad`, `u16 pad`, `u32 réservé`, `u32 taille compressée` |
| frames (36 octets ×n) | `u16 page`, `u16 pad`, `f32 u0, v0, u1, v1` (coordonnées de texture, `v` depuis le bas), `f32 x0, y0, x1, y1` (le quad en unités, relatif à l'origine du sprite) |
| clips | `char name[32]`, `f32 fps`, `u16 loop_start`, `u8 wrap` (mode tk2d : 0 boucle, 1 boucle partielle, 2 une fois…), `u8 pad`, `u16 n`, puis `s16 frame[n]` (`-1` = frame introuvable) |
| données | les pages, compressées en zlib, dans l'ordre |

Le quad reste en unités du jeu quel que soit `--scale` : seule la résolution de la texture change. Les frames
identiques sont stockées une seule fois.

## Textes : `.txt` (`tools/build_text.py`)

Une entrée par ligne, `CLÉ<TAB>texte`, en UTF-8. Dans le texte, `\n` est un retour à la ligne, `\f` un changement de
page et `\\` une barre oblique. Le reste du balisage du jeu est retiré.

## Musique et sons (`tools/build_music.py`)

Ogg Vorbis, qualité 3, 32 kHz. Une musique est le mélange des canaux choisis de son `MusicCue` / `AtmosCue`
(par défaut le canal 0, la couche principale). Le nom du fichier est le nom du cue en minuscules, avec des `_`.
