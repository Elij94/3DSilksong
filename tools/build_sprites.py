#!/usr/bin/env python3
"""Convert tk2d animation libraries of the AssetRipper export into 3DS sprite sets (format: docs/FORMATS.md):
the frames their clips use, cut upright out of the tk2d atlases, packed into GPU-ready texture pages, plus the clips
(fps, loop start, frame list).

  python tools/build_sprites.py <out.sss> "<library name>" [--clips Idle,Run,...] [--scale 0.5] [--fmt rgba4]
  python tools/build_sprites.py --list [pattern]        the animation libraries of the export, with their clip count

<library name> is the prefab's name ("Hornet Anim", "Bone Thumper Anim", ...). Several can be given, separated
by '+', to make one set from several libraries (clip names must then be unique).
Options:
  --clips A,B,C   only these clips (default: all)
  --scale S       resize the frames (default 0.4375: the game's 64 px per unit down to the scenery's 28)
  --fmt F         rgba8 or rgba4 (default rgba4)
  --page N        texture page size (default 512, at most 1024)"""
import os, sys, glob, struct, zlib, argparse, hashlib
import numpy as np
from PIL import Image
from sscommon import EX
import tk2d, ctrtex


def find_library(name):
    hits = [p for p in glob.glob(f'{EX}/**/{glob.escape(name)}.prefab', recursive=True) if tk2d.load_library(p)]
    return hits[0] if hits else None


def pack(sizes, page):
    """shelf packing: [(w, h)] -> [(page, x, y)] (top-left), biggest first"""
    order = sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0]))
    pos = [None] * len(sizes)
    pages = []            # per page: [shelf y, shelf height, x]
    for i in order:
        w, h = sizes[i]
        if w > page or h > page:
            raise SystemExit(f'a frame ({w}x{h}) is bigger than a page ({page}): lower --scale or raise --page')
        for p, st in enumerate(pages):
            if st[2] + w <= page and h <= st[1]:
                pos[i] = (p, st[2], st[0]); st[2] += w; break
            if st[0] + st[1] + h <= page:
                st[0], st[1], st[2] = st[0] + st[1], h, w
                pos[i] = (p, 0, st[0]); break
        else:
            pages.append([0, h, w])
            pos[i] = (len(pages) - 1, 0, 0)
    return pos, len(pages)


def s(txt, n):
    return (txt or '').encode('utf-8')[:n - 1].ljust(n, b'\0')


def build(out, names, clips_wanted, scale, fmt, page):
    clips = {}
    for name in names:
        p = find_library(name)
        if not p:
            raise SystemExit(f'no tk2d animation library named "{name}" in the export (try --list)')
        for cname, c in tk2d.load_library(p).items():
            if clips_wanted and cname not in clips_wanted:
                continue
            if cname in clips:
                raise SystemExit(f'clip "{cname}" is in two of the libraries')
            clips[cname] = c
    missing = [c for c in (clips_wanted or []) if c not in clips]
    if missing:
        print('warning: clips not found:', ', '.join(missing))

    # the frames, each cut once (a frame is a (collection, sprite id) pair), identical images shared
    frame_of, frames, images, seen = {}, [], [], {}
    for c in clips.values():
        for key in c['frames']:
            if key in frame_of:
                continue
            coll = tk2d.collection_by_guid(key[0])
            got = tk2d.cut_sprite(coll, key[1]) if coll else None
            if not got:
                frame_of[key] = -1
                continue
            img, quad = got
            if scale != 1:
                h, w = img.shape[:2]
                img = np.asarray(Image.fromarray(img).resize((max(1, round(w * scale)), max(1, round(h * scale))),
                                                             Image.LANCZOS))
            ys, xs = np.nonzero(img[..., 3])          # trim the transparent border (and move the quad with it)
            if len(xs):
                h, w = img.shape[:2]
                x0, y0, x1, y1 = quad
                ux, uy = (x1 - x0) / w, (y1 - y0) / h
                quad = (x0 + xs.min() * ux, y1 - (ys.max() + 1) * uy, x0 + (xs.max() + 1) * ux, y1 - ys.min() * uy)
                img = img[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
            hsh = hashlib.sha1(img.tobytes() + str(img.shape).encode()).hexdigest()
            if hsh not in seen:
                seen[hsh] = len(images)
                images.append(img)
            frame_of[key] = len(frames)
            frames.append((seen[hsh], quad))
    if not frames:
        raise SystemExit('no frame could be cut (are the atlases in the export?)')

    sizes = [(im.shape[1] + 2, im.shape[0] + 2) for im in images]       # 1 px of padding around each image
    pos, npages = pack(sizes, page)
    canv = [np.zeros((page, page, 4), np.uint8) for _ in range(npages)]
    for im, (p, x, y) in zip(images, pos):
        canv[p][y + 1:y + 1 + im.shape[0], x + 1:x + 1 + im.shape[1]] = im

    head = b'SSSP' + struct.pack('<HHHHH', 1, npages, len(frames), len(clips), 0)
    tables = bytearray()
    for im_i, (x0, y0, x1, y1) in frames:
        p, x, y = pos[im_i]
        h, w = images[im_i].shape[:2]
        u0, u1 = (x + 1) / page, (x + 1 + w) / page
        v1, v0 = 1 - (y + 1) / page, 1 - (y + 1 + h) / page               # v from the bottom (GPU)
        tables += struct.pack('<HH4f4f', p, 0, u0, v0, u1, v1, x0, y0, x1, y1)
    for cname, c in clips.items():
        ids = [frame_of[k] for k in c['frames']]
        tables += s(cname, 32) + struct.pack('<fHBBH', c['fps'], c['loop_start'], c['wrap'], 0, len(ids))
        tables += struct.pack(f'<{len(ids)}h', *ids)
    blobs = [zlib.compress(ctrtex.encode(cv, fmt), 9) for cv in canv]
    page_table = b''.join(struct.pack('<HHBBxxII', page, page, fmt, 0, 0, len(bl)) for bl in blobs)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as fh:
        fh.write(head + page_table + tables + b''.join(blobs))
    lost = sum(1 for v in frame_of.values() if v < 0)
    print(f'{os.path.basename(out)}: {len(clips)} clips, {len(frames)} frames ({len(images)} distinct images), '
          f'{npages} page(s) of {page}x{page}, {os.path.getsize(out) / 1048576:.2f} MB' + (f', {lost} frames lost' if lost else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?')
    ap.add_argument('library', nargs='?')
    ap.add_argument('--clips')
    ap.add_argument('--scale', type=float, default=28 / 64)
    ap.add_argument('--fmt', choices=list(ctrtex.FORMATS), default='rgba4')
    ap.add_argument('--page', type=int, default=512)
    ap.add_argument('--list', nargs='?', const='', metavar='PATTERN')
    a = ap.parse_args()
    if a.list is not None:
        for p in sorted(glob.glob(f'{EX}/**/*.prefab', recursive=True)):
            n = os.path.basename(p)[:-7]
            if a.list.lower() in n.lower() and 'Anim' in n:
                lib = tk2d.load_library(p)
                if lib:
                    print(f'{len(lib):4d} clips  {n}')
        return
    if not a.out or not a.library:
        ap.error('give <out.sss> and a library name (or --list)')
    build(a.out, a.library.split('+'), set(a.clips.split(',')) if a.clips else None, a.scale,
          ctrtex.FORMATS[a.fmt], a.page)


if __name__ == '__main__':
    main()
