# tk2d (2D Toolkit) readers for the AssetRipper export: sprite collections, animation libraries, and cutting a
# sprite out of its atlas upright (tk2d packs some sprites rotated: "flipped"). Same idea as HK3DS's tk2d.py (MIT).
import os, functools
import numpy as np
from PIL import Image
from sscommon import guidmap, yaml_file, f


def _xy(lst):
    return [(f(p.get('x')), f(p.get('y'))) for p in (lst or [])]


@functools.lru_cache(None)
def load_collection(path):
    """tk2dSpriteCollectionData prefab -> dict(name, sprites=[...], textures=[png path per material id])"""
    for t, v in yaml_file(path).values():
        if t == 'MonoBehaviour' and 'spriteDefinitions' in v:
            sprites = [dict(name=sd.get('name') or '', pos=_xy(sd.get('positions')), uvs=_xy(sd.get('uvs')),
                            mat=int(sd.get('materialId') or 0), flipped=int(sd.get('flipped') or 0))
                       for sd in (v.get('spriteDefinitions') or [])]
            texs = [guidmap().get((x or {}).get('guid')) for x in (v.get('textures') or [])]
            return dict(name=v.get('spriteCollectionName') or os.path.basename(path), sprites=sprites, textures=texs,
                        path=path)
    return None


def collection_by_guid(guid):
    p = guidmap().get(guid)
    return load_collection(p) if p and p.endswith('.prefab') else None


@functools.lru_cache(None)
def load_library(path):
    """tk2dSpriteAnimation prefab -> {clip name: dict(fps, loop_start, wrap, frames=[(collection guid, sprite id)])}"""
    for t, v in yaml_file(path).values():
        if t == 'MonoBehaviour' and 'clips' in v:
            clips = {}
            for c in v.get('clips') or []:
                fr = [((x.get('spriteCollection') or {}).get('guid'), int(x.get('spriteId') or 0))
                      for x in (c.get('frames') or [])]
                clips[c.get('name') or ''] = dict(fps=f(c.get('fps')) or 12.0, loop_start=int(c.get('loopStart') or 0),
                                                  wrap=int(c.get('wrapMode') or 0), frames=fr)
            return clips
    return None


@functools.lru_cache(16)
def _atlas(path):
    return np.asarray(Image.open(path).convert('RGBA'))


def cut_sprite(coll, sid, texels_per_unit=None):
    """Sprite sid of a collection, upright, as (RGBA array, (x0, y0, x1, y1) quad in units relative to the sprite's
    origin). Maps each output pixel back to the atlas through the affine positions -> uvs (handles rotated sprites).
    Returns None for an empty or unreadable sprite."""
    if sid < 0 or sid >= len(coll['sprites']):
        return None
    sd = coll['sprites'][sid]
    pos, uvs = np.array(sd['pos'][:4]), np.array(sd['uvs'][:4])
    tex = coll['textures'][sd['mat']] if sd['mat'] < len(coll['textures']) else None
    if len(pos) < 3 or len(uvs) != len(pos) or not tex or not os.path.exists(tex):
        return None
    atlas = _atlas(tex)
    th, tw = atlas.shape[:2]
    # uv = [px py 1] @ A  (least squares over the quad's corners)
    P = np.c_[pos, np.ones(len(pos))]
    A, *_ = np.linalg.lstsq(P, uvs, rcond=None)
    x0, y0 = pos.min(0)
    x1, y1 = pos.max(0)
    if x1 - x0 <= 0 or y1 - y0 <= 0:
        return None
    if texels_per_unit is None:       # the atlas's own resolution
        texels_per_unit = max(np.hypot(*(A[0] * (tw, th))), np.hypot(*(A[1] * (tw, th))))
    w = max(1, int(round((x1 - x0) * texels_per_unit)))
    h = max(1, int(round((y1 - y0) * texels_per_unit)))
    gx = x0 + (np.arange(w) + 0.5) / w * (x1 - x0)
    gy = y1 - (np.arange(h) + 0.5) / h * (y1 - y0)          # top row first
    X, Y = np.meshgrid(gx, gy)
    U = X * A[0, 0] + Y * A[1, 0] + A[2, 0]
    V = X * A[0, 1] + Y * A[1, 1] + A[2, 1]
    ix = np.clip((U * tw).astype(int), 0, tw - 1)
    iy = np.clip(((1 - V) * th).astype(int), 0, th - 1)       # uv origin is the bottom-left corner
    return atlas[iy, ix], (float(x0), float(y0), float(x1), float(y1))
