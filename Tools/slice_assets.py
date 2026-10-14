"""Slice the Codex-made art into the skin files the plugin loads.
Inputs: the pasted images (numbered webp). Output: D:/Pirate EQ/assets/skin/*.png + contact_sheet.png"""
import os, sys
from collections import deque
from PIL import Image

SRC = sys.argv[1]
OUT = r'D:/Pirate EQ/assets/skin'
os.makedirs(OUT, exist_ok=True)

def load(n): return Image.open(os.path.join(SRC, f'{n}.webp')).convert('RGBA')

def bbox_alpha(im, thr=16):
    a = im.getchannel('A').point(lambda v: 255 if v > thr else 0)
    return a.getbbox()

def segments(profile, min_gap=6, min_len=12):
    """runs of 'True' in a boolean profile separated by gaps >= min_gap"""
    segs, start, gap = [], None, 0
    for i, v in enumerate(profile):
        if v:
            if start is None: start = i
            gap = 0
            end = i
        elif start is not None:
            gap += 1
            if gap >= min_gap:
                if end - start + 1 >= min_len: segs.append((start, end + 1))
                start, gap = None, 0
    if start is not None and end - start + 1 >= min_len: segs.append((start, end + 1))
    return segs

def split_alpha(im, axis, thr=16, min_gap=6):
    a = im.getchannel('A').load()
    w, h = im.size
    if axis == 'x':
        prof = [any(a[x, y] > thr for y in range(0, h, 2)) for x in range(w)]
        return [im.crop((s, 0, e, h)) for s, e in segments(prof, min_gap)]
    prof = [any(a[x, y] > thr for x in range(0, w, 2)) for y in range(h)]
    return [im.crop((0, s, w, e)) for s, e in segments(prof, min_gap)]

def trim(im, thr=16):
    b = bbox_alpha(im, thr)
    return im.crop(b) if b else im

def flood_clear(im, is_bg):
    """make background transparent by flood-filling from the image border"""
    w, h = im.size
    px = im.load()
    seen = bytearray(w * h)
    q = deque()
    for x in range(w):
        q.append((x, 0)); q.append((x, h - 1))
    for y in range(h):
        q.append((0, y)); q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        i = y * w + x
        if seen[i]: continue
        seen[i] = 1
        if not is_bg(px[x, y]): continue
        px[x, y] = (0, 0, 0, 0)
        if x > 0: q.append((x - 1, y))
        if x < w - 1: q.append((x + 1, y))
        if y > 0: q.append((x, y - 1))
        if y < h - 1: q.append((x, y + 1))
    return im

near_black = lambda p: max(p[0], p[1], p[2]) < 22
def checker(p):
    r, g, b = p[0], p[1], p[2]
    return max(abs(r - g), abs(g - b), abs(r - b)) < 10 and min(r, g, b) > 110

def save(im, name, size=None):
    if size: im = im.resize(size, Image.LANCZOS)
    im.save(os.path.join(OUT, name))
    print(f'{name:22s} {im.size}')
    return im

out = {}
# 4 background texture (opaque) -> 2240x1540
out['bg_texture.png'] = save(load(4), 'bg_texture.png', (2240, 1540))
# 5 header (real alpha) -> trim width-preserving, 2240 wide
h = load(5); b = bbox_alpha(h)
h = h.crop((0, 0, h.width, b[3]))
out['header.png'] = save(h, 'header.png', (2240, int(h.height * 2240 / h.width)))
# 6 panel frame (opaque, full bleed)
out['panel_frame.png'] = save(load(6), 'panel_frame.png', (512, 512))
# 7 graph frame: drop the black surround
g = flood_clear(load(7), near_black)
out['graph_frame.png'] = save(trim(g), 'graph_frame.png')
# 8 recess: baked checkerboard -> crop to the object, clear checker from the border
r = flood_clear(load(8), checker)
out['recess.png'] = save(trim(r), 'recess.png')
# 9 four buttons: off, hover, pressed, on
bt = trim(load(9)); bw = bt.width / 4.0
btns = [trim(bt.crop((int(i * bw), 0, int((i + 1) * bw), bt.height))) for i in range(4)]
print('buttons found:', len(btns))
for im, n in zip(btns, ['button_off.png', 'button_hover.png', 'button_pressed.png', 'button_on.png']):
    out[n] = save(im, n)
# 10 two columns side by side (opaque): split at the darkest seam near the middle
c = load(10); w, hh = c.size; px = c.load()
mid = min(range(int(w * 0.45), int(w * 0.55)),
          key=lambda x: sum(sum(px[x, y][:3]) for y in range(0, hh, 4)))
out['column.png'] = save(c.crop((0, 0, mid, hh)), 'column.png')
out['column_selected.png'] = save(c.crop((mid, 0, w, hh)), 'column_selected.png')
# 11 sheet: row 1 = combo + arrow ; row 2 = badge_off, badge_on, checkbox_off, checkbox_on
rows = split_alpha(load(11), 'y', min_gap=10)
print('sheet rows:', len(rows))
r1 = [trim(x) for x in split_alpha(rows[0], 'x', min_gap=10)]
r2 = [trim(x) for x in split_alpha(rows[1], 'x', min_gap=10)]
print('row1 pieces:', len(r1), 'row2 pieces:', len(r2))
out['combo.png'] = save(r1[0], 'combo.png')
out['combo_arrow.png'] = save(r1[1], 'combo_arrow.png')
for im, n in zip(r2, ['badge_off.png', 'badge_on.png', 'checkbox_off.png', 'checkbox_on.png']):
    out[n] = save(im, n)
# 12 knob (real alpha)
out['knob.png'] = save(trim(load(12)), 'knob.png', (384, 384))
# 13 meters on black: split, clear black surround
m = flood_clear(load(13), near_black)
mt = trim(m); mw, mh = mt.size; mp = mt.load()
seam = min(range(int(mw * 0.4), int(mw * 0.6)), key=lambda x: sum(mp[x, y][3] + sum(mp[x, y][:3]) for y in range(0, mh, 4)))
ms = [trim(mt.crop((0, 0, seam, mh))), trim(mt.crop((seam, 0, mw, mh)))]
print('meters found:', len(ms))
out['meter_off.png'] = save(ms[0], 'meter_off.png')
out['meter_on.png'] = save(ms[1].resize(ms[0].size, Image.LANCZOS), 'meter_on.png')
# 14 reference
load(14).convert('RGB').save(os.path.join(OUT, 'reference.png'))

# contact sheet on mid grey
pieces = [(n, im) for n, im in out.items() if n not in ('bg_texture.png',)]
W = 1800; x = y = 10; rowh = 0
sheet = Image.new('RGBA', (W, 2600), (110, 110, 110, 255))
for n, im in pieces:
    t = im.copy(); t.thumbnail((420, 420))
    if x + t.width > W - 10: x = 10; y += rowh + 30; rowh = 0
    sheet.alpha_composite(t, (x, y))
    x += t.width + 20; rowh = max(rowh, t.height)
sheet = sheet.crop((0, 0, W, y + rowh + 20))
sheet.convert('RGB').save(os.path.join(OUT, 'contact_sheet.png'))
print('contact sheet', sheet.size)
