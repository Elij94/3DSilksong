#!/usr/bin/env python3
"""The shops (ShopOwner components: an NPC's stock of ShopItem assets) of every converted room, for source/shop.c:

- text/shops.txt: "S <scene> <object name> <stock id>" per shop owner, then the stocks:
  "K <stock id>" and per item, in the game's order:
  "I <sheet> <name key> <sheet> <desc key> <cost> <currency> <type flags> <frame> <pd bool> <saved item> <flags>
     <appear> <extra bools> <extra ints> <event>"
  cost: costReference's value, else cost; currency 0 rosaries, 1 shell shards; frame: the item's icon in
  ui/shop_items.sss (-1 none); pd bool: the PlayerData bool bought sets and that marks it bought (-); saved item: the
  item it gives, "T:<name>" a tool (bought once), "C:<name>" a collectable (counted), -; flags: 1 needs an item or tools we can't give yet (not shown), 2 upgrade of an item (not shown),
  4 quest conditions (not shown); appear: extraAppearConditions, groups (any passes) split by '|', their tests (all
  pass) by ',': "b:<field>:<0|1>" or "i:<field>:<numType>:<value>" (numType 0 =, 1 !=, 2 <, 3 >), - for none;
  extra bools: setExtraPlayerDataBools, comma-separated; extra ints: setExtraPlayerDataInts, "<field>:<op>:<n>"
  (op 0 add, 1 subtract, 2 multiply, 3 set); event: eventAfterPurchased (-). Spaces in a value become '\x1f'.
- ui/shop_items.sss: the items' icons, 48 pixels tall at most (clip "<frame>").

  python tools/build_shops.py <romfs dir>"""
import os, sys, glob
import numpy as np
from PIL import Image
from sscommon import guidmap, f
from build_room import Scene, find_scene, sprite_image
from build_sprites import add_image, write_set
import ctrtex
import yaml

ICON = 48


def load_asset(guid):
    p = guidmap().get(guid)
    if not p or not os.path.exists(p):
        return None
    txt = open(p, encoding='utf-8').read()
    body = txt.split('\n', 3)[3] if txt.startswith('%YAML') else txt
    try:
        doc = yaml.load(body, Loader=getattr(yaml, 'CSafeLoader', yaml.SafeLoader))
    except Exception:
        return None
    return (doc or {}).get('MonoBehaviour')


def w(s):
    s = '' if s is None else str(s)
    return s.replace(' ', '\x1f') if s else '-'


def tests(cond):
    groups = []
    for g in (cond or {}).get('TestGroups') or []:
        ts = []
        for t in g.get('Tests') or []:
            typ, field = int(f(t.get('Type'))), t.get('FieldName') or ''
            if not field:
                continue
            if typ == 0:
                ts.append(f"b:{field}:{int(f(t.get('BoolValue')))}")
            elif typ in (1, 2, 3):   # int, float, enum: as numbers
                v = t.get('FloatValue') if typ == 2 else t.get('IntValue')
                ts.append(f"i:{field}:{int(f(t.get('NumType')))}:{f(v)}")
            else:
                ts.append('x')          # a string test: can't pass here
        groups.append(','.join(ts) or 'T')
    return '|'.join(groups) or '-'


def main():
    out = sys.argv[1]
    gm = guidmap()
    shops, stocks, stock_ix = [], [], {}
    frames, images, seen, clips, icon_ix = [], [], {}, {}, {}

    def icon(ref, scale):
        g = (ref or {}).get('guid')
        if not g:
            return -1
        key = (g, (ref or {}).get('fileID'))
        if key in icon_ix:
            return icon_ix[key]
        got = sprite_image(g, (ref or {}).get('fileID'))
        if not got:
            icon_ix[key] = -1
            return -1
        img, _ = got
        h, wd = img.shape[:2]
        k = min(1.0, ICON / max(h, wd))
        nw, nh = max(1, round(wd * k)), max(1, round(h * k))
        if (nw, nh) != (wd, h):
            img = np.asarray(Image.fromarray(img).resize((nw, nh), Image.LANCZOS))
        # the quad in pixels, centred (drawn at 1 unit = 1 pixel)
        fr = add_image(img, (-nw / 2, -nh / 2, nw / 2, nh / 2), frames, images, seen)
        clips[str(fr)] = dict(fps=12, loop_start=0, wrap=6, ids=[fr])
        icon_ix[key] = fr
        return fr

    def stock_of(refs, key):
        if key in stock_ix:
            return stock_ix[key]
        lines = []
        for r in refs:
            it = load_asset((r or {}).get('guid'))
            if not it:
                continue
            cost = int(f(it.get('cost') or 0))
            cr = (it.get('costReference') or {}).get('guid')
            if cr:
                c = load_asset(cr)
                if c and 'value' in c:
                    cost = int(f(c['value']))
            flags = 0
            if (it.get('requiredItem') or {}).get('guid') or int(f(it.get('requiredTools') or 0)):
                flags |= 1
            if (it.get('upgradeFromItem') or {}).get('guid'):
                flags |= 2
            if it.get('questsAppearConditions'):
                flags |= 4
            saved = '-'
            sg = (it.get('savedItem') or {}).get('guid')
            if sg:
                p = gm.get(sg)
                if p:   # T: a tool (one of a kind), C: a collectable (counted)
                    kind = 'T' if 'Tool Items' in p.replace(chr(92), '/').split('/') else 'C'
                    saved = kind + ':' + os.path.splitext(os.path.basename(p))[0]
            dn, ds = it.get('displayName') or {}, it.get('description') or {}
            ints = []
            for op in it.get('setExtraPlayerDataInts') or []:
                if op.get('variableName'):
                    ints.append(f"{op['variableName']}:{int(f(op.get('operation') or 0))}:{int(f(op.get('number') or 0))}")
            bools = [b for b in (it.get('setExtraPlayerDataBools') or []) if b]
            lines.append(' '.join([
                'I', w(dn.get('Sheet')), w(dn.get('Key')), w(ds.get('Sheet')), w(ds.get('Key')), str(cost),
                str(int(f(it.get('currencyType') or 0))), str(int(f(it.get('typeFlags') or 0))),
                str(icon(it.get('itemSprite'), f(it.get('itemSpriteScale') or 1))), w(it.get('playerDataBoolName')),
                w(saved), str(flags), w(tests(it.get('extraAppearConditions'))), w(','.join(bools)),
                w(','.join(ints)), w(it.get('eventAfterPurchased'))]))
        stock_ix[key] = len(stocks)
        stocks.append(lines)
        return stock_ix[key]

    rooms = sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(out, 'rooms', '*.ssr')))
    for name in rooms:
        path = find_scene(name)
        if not path:
            continue
        raw = open(path, encoding='utf-8', errors='replace').read()
        if 'shopPrefab:' not in raw:
            continue
        sc = Scene(path)
        for gid, comps in sc.comps.items():
            for t, v in comps:
                if t != 'MonoBehaviour' or 'shopPrefab' not in v or ('stock' not in v and 'stockList' not in v):
                    continue
                sl = (v.get('stockList') or {}).get('guid')
                if sl:
                    lst = load_asset(sl) or {}
                    refs, key = lst.get('shopItems') or [], sl
                else:
                    refs = v.get('stock') or []
                    key = tuple((r or {}).get('guid') for r in refs)
                k = stock_of(refs, key)
                shops.append(f"S {name} {w(sc.go[gid].get('m_Name'))} {k}")
    text = shops[:]
    for i, lines in enumerate(stocks):
        text.append(f'K {i}')
        text += lines
    os.makedirs(os.path.join(out, 'text'), exist_ok=True)
    open(os.path.join(out, 'text', 'shops.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(text) + '\n')
    if frames:
        os.makedirs(os.path.join(out, 'ui'), exist_ok=True)
        write_set(os.path.join(out, 'ui', 'shop_items.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])
    print(f'{len(shops)} shop owners, {len(stocks)} stocks, {sum(map(len, stocks))} items, {len(images)} icons')


if __name__ == '__main__':
    main()
