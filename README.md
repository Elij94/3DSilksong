# 3DSilksong

Outils de conversion de **Hollow Knight: Silksong** vers des données pour la **New 3DS / New 2DS XL** (écran du haut
en 800x240, 124 Mo de RAM), sur le modèle des outils de [HK3DS](https://github.com/silviognotti/HK3DS).

> Projet non officiel, **non affilié à Team Cherry**. Ce dépôt ne contient **aucun fichier du jeu** (ni image, ni son,
> ni texte, ni données) : seulement des outils qui convertissent **votre propre copie PC** du jeu, sur votre machine.
> Il faut posséder Silksong pour s'en servir.

## Ce que font les outils

1. **Export** : `tools/export_game.sh` pilote AssetRipper (headless) sur le dossier `_Data` de votre copie.
2. **Conversion** (`tools/`, Python) :
   - **salles** (`build_room.py`) : grille de collision, sorties (`TransitionPoint`), points de réapparition, zones
     de dégâts, verrous de caméra, et le décor dessiné en 6 couches de parallaxe, découpé en tuiles GPU 3DS ;
   - **sprites** (`build_sprites.py`) : n'importe quelle bibliothèque d'animations tk2d (héroïne, ennemis, PNJ) en
     pages de texture GPU avec ses clips ;
   - **musique et sons** (`build_music.py`) : cues de musique et d'ambiance mixés en Ogg Vorbis ;
   - **textes** (`build_text.py`) : les feuilles de langue du jeu, déchiffrées, en fichiers `CLÉ<TAB>texte`.
3. **Paquet** : tout est réuni dans un seul fichier RomFS, `3dsilksong.romfs`, à copier dans `SD:/3ds/3dsilksong/`.

`tools/build.sh` enchaîne tout. Les formats sont décrits dans [docs/FORMATS.md](docs/FORMATS.md), et
`tools/inspect_data.py` relit et vérifie les fichiers produits.

`source/` contient seulement un squelette 3DS : il monte le fichier de données depuis la carte SD (`romfsMountFromFile`)
et passe l'écran en 800x240. C'est le point de départ d'un moteur qui lirait ces formats.

## Prérequis

- Une copie PC de Silksong (Steam ou Game Pass ; les fichiers Game Pass sont lisibles).
- Python 3.11+ avec `numpy`, `Pillow`, `PyYAML`, `cryptography`, et `UnityPy` pour `bundle_deps.py` ; `ffmpeg` dans le PATH.
- [AssetRipper](https://github.com/AssetRipper/AssetRipper) 2.0.0.
- `mkromfs3ds`, ou un équivalent (`MKROMFS=...`), pour le fichier RomFS.
- Pour compiler `source/` : devkitPro avec `3ds-dev` (et `makerom`, `bannertool` pour le CIA).
- Disque : ~60 Go pour l'export complet. Mémoire : 16 à 24 Go libres pendant l'export AssetRipper.

## Utilisation

```sh
export SILKSONG="C:/XboxGames/Hollow Knight- Silksong"   # le dossier d'installation
tools/build.sh                                             # tout : inventaire, export, index, conversion, RomFS
```

Ou étape par étape :

```sh
python tools/inventory.py "$SILKSONG"                      # aperçu en quelques secondes, sans export
tools/export_game.sh "$SILKSONG"                           # export AssetRipper dans ../ssdata (long)
python tools/index_export.py
python tools/build_room.py romfs Tut_01 --preview tut01.png   # une salle (et un aperçu de sa géométrie)
python tools/build_sprites.py --list                       # les bibliothèques d'animations de l'export
python tools/build_sprites.py romfs/sprites/hero.sss "Hornet Anim"
python tools/build_music.py romfs
python tools/build_text.py romfs EN FR
python tools/inspect_data.py romfs/rooms/Tut_01.ssr romfs/sprites/hero.sss
```

Pour essayer sur une seule salle sans tout exporter (~700 Mo, quelques minutes) :

```sh
python tools/bundle_deps.py --depth 1 --extra hero --subset ../test_tut01 Tut_01
SSDATA=../test_tut01_out tools/export_game.sh ../test_tut01
echo Tut_01 > rooms.txt
SSDATA=../test_tut01_out tools/build.sh --rooms rooms.txt
```

Sous Windows, les scripts tournent dans Git Bash. `SSDATA` choisit le dossier des exports (par défaut `../ssdata`,
à côté du dépôt). Ce dossier, `work/`, `romfs/` et tout ce qui est produit sont ignorés par git.

### Bon à savoir

- Les bundles de Silksong n'indiquent pas leur version Unity : `export_game.sh` règle `DefaultVersion = 6000.0.50f1`
  dans AssetRipper, sans quoi aucune salle n'est exportée (voir [docs/EXPLORATION.md](docs/EXPLORATION.md)).
- Les options de `build_room.py` (`--ppu`, `--cpu`, `--fmt`) règlent la finesse du décor et des collisions, et donc la
  taille des données. Avec les réglages par défaut, `Tut_01` fait 6,4 Mo de décor (1210 tuiles RGBA4).

## Organisation

```
tools/         export, conversion, paquet (Python / bash)
tools/data/    all_rooms.txt (les 590 scènes), sprites.txt (les jeux de sprites que build.sh produit)
source/        squelette 3DS (montage des données, écran HD)
cia/           RSF du CIA
docs/          EXPLORATION.md (ce que contient le jeu), FORMATS.md (les fichiers produits)
```

## Licence

MIT pour le code de ce dépôt (voir [LICENSE](LICENSE)). Une partie de la structure (Makefile, RSF, l'ordre Morton des
textures, `sscommon.py`, `index_export.py`, le déchiffrement des textes) est adaptée de HK3DS, lui aussi sous licence MIT.
Hollow Knight: Silksong appartient à Team Cherry.
