#!/usr/bin/env python3
"""The game's language sheets -> <out>/text/<lang>/<Sheet>.txt, one 'KEY<TAB>text' line per entry, UTF-8.
The sheets are Resources/languages/<LANG>_<Sheet>.txt of the export: base64, AES-ECB encrypted XML
(<entry name="KEY">text</entry>), the same scheme as Hollow Knight (HK3DS's text_sheets.py).
In the text: pages are separated by '\\f' (<page>), line breaks are '\\n' (<br>), other markup is dropped.

  python tools/build_text.py <out_dir> [LANG ...]        (default: EN FR; the export has DE EN ES FR IT JA KO PT RU ZH)
Needs the 'cryptography' package."""
import os, re, sys, glob, html, base64
from sscommon import EX

KEY = b'UKu52ePUBwetZ9wNX88o54dnfKRu0T1l'


def decrypt(raw):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    d = Cipher(algorithms.AES(KEY), modes.ECB()).decryptor()
    x = d.update(base64.b64decode(raw)) + d.finalize()
    return x[:-x[-1]].decode('utf-8')


def clean(t):
    t = html.unescape(t)
    t = re.sub(r'<(?:h?page)>', '\f', t).replace('<br>', '\n')
    t = re.sub(r'<[^>]{1,40}>', '', t)
    return t.replace('\r', '').replace('\t', ' ').strip()


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    langs = [l.upper() for l in sys.argv[2:]] or ['EN', 'FR']
    src = os.path.join(EX, 'Resources', 'languages')
    if not os.path.isdir(src):
        raise SystemExit(f'no {src} (export the game first)')
    for lang in langs:
        files = sorted(glob.glob(os.path.join(src, f'{lang}_*.txt')))
        if not files:
            print(f'{lang}: no sheet')
            continue
        d = os.path.join(out, 'text', lang.lower())
        os.makedirs(d, exist_ok=True)
        n = bad = 0
        for p in files:
            sheet = os.path.basename(p)[len(lang) + 1:-4]
            try:
                xml = decrypt(open(p, 'rb').read())
            except Exception:
                bad += 1
                continue
            lines = []
            for key, txt in re.findall(r'<entry name="([^"]+)">(.*?)</entry>', xml, re.S):
                t = clean(txt).replace('\\', '\\\\').replace('\n', '\\n').replace('\f', '\\f')
                lines.append(f'{key}\t{t}')
            with open(os.path.join(d, sheet + '.txt'), 'w', encoding='utf-8', newline='\n') as fh:
                fh.write('\n'.join(lines) + '\n')
            n += len(lines)
        print(f'{lang}: {len(files) - bad} sheets, {n} entries' + (f', {bad} unreadable' if bad else ''))


if __name__ == '__main__':
    main()
