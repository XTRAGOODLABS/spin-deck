"""Puts the Spin Deck logo where the app showed the Mixxx logo: the launch screen,
the skin toolbars, the About box and the app icon. Run after make-icon.py:
python3 packaging/spindeck/brand-skins.py
Each logo file keeps its size; the Spin Deck art is centred inside it."""
import base64, io, os, re
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)
LOCKUP = Image.open('packaging/spindeck/lockup-big.png')
ICON = Image.open('packaging/spindeck/icon.png')

# file -> which art goes in it
TARGETS = {
    'res/images/mixxx_logo.svg': 'lockup',
    'res/images/skins/LateNight/mixxx_logo_24px.svg': 'lockup',
    'res/images/icons/scalable/apps/mixxx.svg': 'icon',
    'res/skins/Deere/image/mixxx_logo.svg': 'lockup',
    'res/skins/LateNight/palemoon/style/mixxx_logo.svg': 'lockup',
    'res/skins/LateNight/palemoon/style/mixxx_logo_small.svg': 'lockup',
    'res/skins/LateNight/classic/style/mixxx_logo.svg': 'lockup',
    'res/skins/LateNight/classic/style/mixxx_logo_small.svg': 'lockup',
}


def size_of(svg_text, fallback):
    head = re.search(r'<svg\b[^>]*>', svg_text, re.S)
    head = head.group(0) if head else ''
    num = lambda name: (re.search(r'\b' + name + r'="([0-9.]+)', head) or [None, None])[1]
    w, h = num('width'), num('height')
    if w and h:
        return float(w), float(h)
    box = re.search(r'viewBox="([-0-9. ]+)"', head)
    if box:
        parts = [float(x) for x in box.group(1).split()]
        if len(parts) == 4 and parts[2] > 0 and parts[3] > 0:
            return parts[2], parts[3]
    return fallback


def png_data(img, max_w, max_h):
    scale = min(max_w / img.width, max_h / img.height)
    w, h = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    # Store at 4x so it stays sharp on Retina screens.
    big = img.resize((max(1, w * 4), max(1, h * 4)), Image.LANCZOS)
    buf = io.BytesIO()
    big.save(buf, 'PNG', optimize=True)
    return w, h, base64.b64encode(buf.getvalue()).decode()


for path, art in TARGETS.items():
    if not os.path.exists(path):
        print('skip (missing):', path)
        continue
    old = open(path, encoding='utf-8', errors='ignore').read()
    img = LOCKUP if art == 'lockup' else ICON
    W, H = size_of(old, (img.width / 4, img.height / 4))
    pad = 0.08 if art == 'lockup' else 0
    w, h, data = png_data(img, W * (1 - pad), H * (1 - pad))
    x, y = (W - w) / 2, (H - h) / 2
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{W:g}" height="{H:g}" viewBox="0 0 {W:g} {H:g}">\n'
        f'  <!-- Spin Deck by XTRAGOODLABS -->\n'
        f'  <image x="{x:g}" y="{y:g}" width="{w}" height="{h}" '
        f'xlink:href="data:image/png;base64,{data}"/>\n'
        f'</svg>\n')
    open(path, 'w', encoding='utf-8').write(svg)
    print('branded:', path, f'{W:g}x{H:g}')
