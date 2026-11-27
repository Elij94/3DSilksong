"""Draw the README's progress bars as small SVG files in docs/progress/.

    python tools/progress_bars.py 95 85 80 30 0 --overall 65
"""
import argparse
import pathlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "progress"


def colours(p):
    if p >= 85:
        return "#2ea043", "#56d364"  # green
    if p >= 60:
        return "#1f6feb", "#58a6ff"  # blue
    if p >= 35:
        return "#d29922", "#e3b341"  # amber
    return "#da3633", "#f85149"      # red


def bar(p, w, h):
    a, b = colours(p)
    r = h / 2
    fw = round(w * p / 100, 1)
    fill = ""
    if p > 0:
        fill = (f'<rect width="{max(fw, h)}" height="{h}" rx="{r}" fill="url(#g)"/>'
                f'<rect x="{r}" y="1" width="{max(fw - h, 0)}" height="{h * 0.3:.1f}" rx="{h * 0.15:.1f}" '
                f'fill="#fff" opacity=".22"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
            f'<defs><linearGradient id="g" x1="0" x2="1"><stop offset="0" stop-color="{a}"/>'
            f'<stop offset="1" stop-color="{b}"/></linearGradient></defs>'
            f'<rect width="{w}" height="{h}" rx="{r}" fill="#8b949e" opacity=".25"/>{fill}</svg>\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("percents", type=int, nargs="*")
    ap.add_argument("--overall", type=int)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for p in set(args.percents):
        (OUT / f"{p}.svg").write_text(bar(p, 160, 10), encoding="utf-8")
    if args.overall is not None:
        (OUT / "overall.svg").write_text(bar(args.overall, 480, 18), encoding="utf-8")


if __name__ == "__main__":
    main()
