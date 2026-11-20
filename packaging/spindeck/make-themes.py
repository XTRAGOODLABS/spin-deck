"""Spin Deck color themes for the LateNight skin.

Builds extra color schemes from LateNight's PaleMoon scheme by recoloring it:
greys get the theme's tint, blue/cyan accents become the theme's main accent,
orange/amber accents its second accent. Reds and greens (record, loop, sync
states) keep their meaning. Run from the repo root:

    python3 packaging/spindeck/make-themes.py

It rewrites res/skins/LateNight/style_spindeck_*.qss and the Spin Deck
<Scheme> blocks in res/skins/LateNight/skin.xml. Safe to run again.
"""
import colorsys
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIN = os.path.join(ROOT, "res/skins/LateNight")

# name, file id, neutral hue/sat, dark multiplier, main accent hue/sat, second accent hue/sat
THEMES = [
    dict(name="Spin Deck", id="crate", nh=0, ns=0.0, dark=0.72, ph=148, ps=0.70, sh=148, ss=0.55),
    dict(name="Neon Night", id="neon", nh=238, ns=0.30, dark=0.85, ph=300, ps=0.85, sh=185, ss=0.85),
    dict(name="Vinyl Gold", id="gold", nh=30, ns=0.20, dark=0.90, ph=43, ps=0.80, sh=22, ss=0.75),
    dict(name="Ice", id="ice", nh=210, ns=0.16, dark=0.95, ph=195, ps=0.75, sh=222, ss=0.65),
]

BEGIN = "<!-- SPINDECK THEMES BEGIN (made by packaging/spindeck/make-themes.py) -->"
END = "<!-- SPINDECK THEMES END -->"


def clamp(v, lo=0.0, hi=1.0):
    return max(lo, min(hi, v))


def recolor(r, g, b, t):
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    deg = h * 360
    if s < 0.18 or l < 0.05 or l > 0.95:
        # A grey: tint it, and deepen the darks a little for themes that want it.
        nl = l * t["dark"] if l < 0.5 else l
        ns = t["ns"] * clamp(1 - abs(nl - 0.5) * 1.1)
        h2, l2, s2 = t["nh"] / 360, nl, ns
    elif 165 <= deg <= 265:
        h2, l2, s2 = t["ph"] / 360, l, max(s * 0.6, t["ps"])
    elif 15 <= deg <= 62:
        h2, l2, s2 = t["sh"] / 360, l, max(s * 0.6, t["ss"])
    else:
        return r, g, b  # reds, greens, magentas keep their meaning
    r2, g2, b2 = colorsys.hls_to_rgb(h2, clamp(l2), clamp(s2))
    return round(r2 * 255), round(g2 * 255), round(b2 * 255)


HEX = re.compile(r"#([0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b")
RGBA = re.compile(r"rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(,\s*[\d.]+\s*)?\)")


def recolor_text(text, t):
    def hex_sub(m):
        v = m.group(1)
        if len(v) == 3:
            r, g, b = (int(c * 2, 16) for c in v)
            alpha = ""
        elif len(v) == 8:  # Qt #AARRGGBB
            alpha = v[:2]
            r, g, b = int(v[2:4], 16), int(v[4:6], 16), int(v[6:8], 16)
        else:
            alpha = ""
            r, g, b = int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16)
        r, g, b = recolor(r, g, b, t)
        return "#" + alpha + f"{r:02x}{g:02x}{b:02x}"

    def rgba_sub(m):
        r, g, b = recolor(int(m.group(1)), int(m.group(2)), int(m.group(3)), t)
        rest = m.group(4) or ""
        kind = "rgba" if rest else "rgb"
        return f"{kind}({r}, {g}, {b}{rest})"

    return RGBA.sub(rgba_sub, HEX.sub(hex_sub, text))


# Only these scheme variables are recolored; cue, loop and waveform band colors keep meaning.
SCHEME_VARS = re.compile(
    r'(<SetVariable name="(?:BgColor[^"]*|SignalColor_[^"]*|PlayPosColor|AxesColor|BeatColor|'
    r'Arc[^"]*|Bar[^"]*)">)([^<]*)(</SetVariable>)')


def main():
    xml_path = os.path.join(SKIN, "skin.xml")
    xml = open(xml_path, encoding="utf-8").read()
    if BEGIN in xml:
        a = xml.index(BEGIN)
        b = xml.index(END) + len(END)
        xml = xml[:a].rstrip() + "\n" + xml[b:].lstrip("\n")
    start = xml.index("<Scheme>")
    end = xml.index("</Scheme><!-- PaleMoon -->") + len("</Scheme>")
    base = xml[start:end]
    qss = open(os.path.join(SKIN, "style_palemoon.qss"), encoding="utf-8").read()

    blocks = []
    for t in THEMES:
        out_qss = "/* Spin Deck theme: %s. Made from style_palemoon.qss by make-themes.py. */\n" % t["name"]
        out_qss += recolor_text(qss, t)
        open(os.path.join(SKIN, "style_spindeck_%s.qss" % t["id"]), "w", encoding="utf-8").write(out_qss)
        block = base.replace("<Name>PaleMoon</Name>", "<Name>%s</Name>" % t["name"], 1)
        block = block.replace("skins:LateNight/style_palemoon.qss",
                              "skins:LateNight/style_spindeck_%s.qss" % t["id"])
        block = SCHEME_VARS.sub(lambda m: m.group(1) + recolor_text(m.group(2), t) + m.group(3), block)
        # The launch screen background
        block = re.sub(r"(LaunchImage \{\s*background-color: )(#[0-9a-fA-F]{3,8})",
                       lambda m: m.group(1) + recolor_text(m.group(2), t), block, count=1)
        blocks.append("    " + block + "<!-- %s -->" % t["name"])

    insert = "    " + BEGIN + "\n" + "\n".join(blocks) + "\n    " + END + "\n"
    xml = xml.replace("  </Schemes>", insert + "  </Schemes>", 1)
    open(xml_path, "w", encoding="utf-8").write(xml)
    print("themes:", ", ".join(t["name"] for t in THEMES))


if __name__ == "__main__":
    main()
