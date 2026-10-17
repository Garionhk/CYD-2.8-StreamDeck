#!/usr/bin/env python3
"""
make_icons.py -- 72x72 PNG button icons for Companion page 1.

    python3 tools/make_icons.py

Writes icons/*.png (one per button) and icons/preview.png, a sheet showing
each icon at 72 px (a Stream Deck / the deck's 4x3 layout) and at 36 px (the
deck's 8x4 layout, box-filtered the way the deck does it).

Design rules, for reading at a glance on a small screen:
  - one bold white pictogram on a solid colour, no fine detail
  - a short label (one or two words, capitals, condensed bold) on a dark band
  - colour = kind of source: blue cameras, purple picture-in-picture,
    raspberry the Raspberry Pi, teal the computer, amber pre-show, red FTB

Edit BUTTONS below to change a label or colour and run it again. Needs Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageFont

SIZE = 72
SS = 6                      # drawn at 6x and shrunk: smooth edges at 72 px
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")

FONT = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf"
FONT_FALLBACK = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

BLUE, PURPLE, RASPBERRY = (21, 101, 192), (106, 27, 154), (197, 26, 74)
TEAL, AMBER, BLACK = (0, 121, 107), (230, 81, 0), (12, 12, 12)
WHITE, RED = (255, 255, 255), (229, 57, 53)


# ---------------------------------------------------------------------------
# Drawing in 72-unit coordinates
# ---------------------------------------------------------------------------
class Pen:
    def __init__(self, bg):
        self.img = Image.new("RGB", (SIZE * SS, SIZE * SS), bg)
        self.d = ImageDraw.Draw(self.img)
        self.bg = bg

    @staticmethod
    def s(*v):
        return [round(x * SS) for x in v]

    def rect(self, x0, y0, x1, y1, fill=None, outline=None, width=0, r=0):
        self.d.rounded_rectangle(self.s(x0, y0, x1, y1), radius=r * SS, fill=fill,
                                 outline=outline, width=round(width * SS))

    def circle(self, cx, cy, r, fill=None, outline=None, width=0):
        self.d.ellipse(self.s(cx - r, cy - r, cx + r, cy + r), fill=fill, outline=outline,
                       width=round(width * SS))

    def poly(self, pts, fill):
        self.d.polygon([round(c * SS) for p in pts for c in p], fill=fill)

    def line(self, pts, fill, width):
        self.d.line([round(c * SS) for p in pts for c in p], fill=fill, width=round(width * SS),
                    joint="curve")

    def arc(self, cx, cy, r, start, end, fill, width):
        self.d.arc(self.s(cx - r, cy - r, cx + r, cy + r), start, end, fill=fill,
                   width=round(width * SS))

    def label(self, text, band=(0, 0, 0, 110)):
        # Dark band across the bottom, label centred in it, as large as fits.
        overlay = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        ImageDraw.Draw(overlay).rectangle(self.s(0, 50, SIZE, SIZE), fill=band)
        self.img = Image.alpha_composite(self.img.convert("RGBA"), overlay).convert("RGB")
        self.d = ImageDraw.Draw(self.img)
        path = FONT if os.path.exists(FONT) else FONT_FALLBACK
        for px in range(20, 9, -1):
            font = ImageFont.truetype(path, px * SS)
            l, t, r, b = self.d.textbbox((0, 0), text, font=font)
            if r - l <= (SIZE - 6) * SS:
                break
        x = (SIZE * SS - (r - l)) / 2 - l
        y = 61 * SS - (b + t) / 2
        self.d.text((x, y), text, font=font, fill=WHITE)

    def done(self):
        return self.img.resize((SIZE, SIZE), Image.LANCZOS)


# ---------------------------------------------------------------------------
# Pictograms (white, in the top 50 px)
# ---------------------------------------------------------------------------
def camera(p, x=0, y=0, k=1.0):
    """Video camera: body, lens, viewfinder wedge."""
    def P(a, b): return (x + a * k, y + b * k)
    p.rect(*P(12, 14), *P(46, 40), fill=WHITE, r=5 * k)
    p.poly([P(46, 24), P(60, 15), P(60, 39), P(46, 30)], fill=WHITE)
    p.circle(*P(29, 27), 7.5 * k, fill=p.bg)
    p.circle(*P(29, 27), 3.5 * k, fill=WHITE)


def monitor(p, x0, y0, x1, y1, stand=True):
    p.rect(x0, y0, x1, y1, outline=WHITE, width=3.2, r=3)
    if stand:
        cx = (x0 + x1) / 2
        p.rect(cx - 2, y1, cx + 2, y1 + 4, fill=WHITE)
        p.rect(cx - 8, y1 + 3.5, cx + 8, y1 + 6, fill=WHITE, r=1)


def speaker_waves(p, cx, cy, n=2, r0=6, gap=6):
    for i in range(n):
        p.arc(cx, cy, r0 + i * gap, -50, 50, WHITE, 3)


def speaker(p, x, y):
    """Speaker cone facing right, top-left at (x, y), ~12 x 20."""
    p.rect(x, y + 6, x + 6, y + 14, fill=WHITE, r=1)
    p.poly([(x + 5, y + 6), (x + 13, y), (x + 13, y + 20), (x + 5, y + 14)], fill=WHITE)


def play(p, cx, cy, h, fill=WHITE):
    p.poly([(cx - h * 0.45, cy - h / 2), (cx - h * 0.45, cy + h / 2), (cx + h * 0.55, cy)], fill=fill)


# ---------------------------------------------------------------------------
# The nine buttons (page 1, as Companion lists them)
# ---------------------------------------------------------------------------
def main_cam():
    p = Pen(BLUE)
    camera(p, 1, 1)
    p.label("MAIN CAM")
    return p.done()


def computer_pip_cam1():
    p = Pen(PURPLE)
    monitor(p, 8, 6, 64, 39)
    p.rect(36, 19, 59, 35, fill=WHITE, r=2)          # the camera inset
    p.circle(47.5, 27, 4.2, fill=PURPLE)
    p.circle(47.5, 27, 1.8, fill=WHITE)
    p.label("PC + CAM 1")
    return p.done()


def raspberry_sound_only():
    p = Pen(RASPBERRY)
    play(p, 18, 25, 24)                            # video plays...
    p.rect(30, 12, 32.5, 38, fill=WHITE, r=1)      # ...but only its sound goes out
    speaker(p, 37, 15)
    speaker_waves(p, 51, 25, n=2, r0=5, gap=6)
    p.label("PI AUDIO")
    return p.done()


def main_cam_no_logo():
    p = Pen(BLUE)
    camera(p, -2, 3, 0.95)
    # "no logo": a badge with a slash, top right
    p.circle(56, 14, 10, fill=BLUE)
    p.circle(56, 14, 8, outline=WHITE, width=3)
    p.line([(50.5, 19.5), (61.5, 8.5)], WHITE, 3)
    p.label("CAM NO LOGO")
    return p.done()


def left_circle_pip():
    p = Pen(PURPLE)
    monitor(p, 8, 6, 64, 39)
    p.circle(23, 22.5, 10.5, fill=WHITE)
    p.circle(23, 22.5, 4, fill=PURPLE)
    p.circle(23, 22.5, 1.8, fill=WHITE)
    p.label("CIRCLE PIP")
    return p.done()


def before_start():
    p = Pen(AMBER)
    p.circle(36, 25, 19, outline=WHITE, width=4)
    p.line([(36, 25), (36, 13)], WHITE, 4)         # minute hand
    p.line([(36, 25), (45, 30)], WHITE, 4)         # hour hand
    p.circle(36, 25, 3, fill=WHITE)
    p.label("BEFORE START")
    return p.done()


def pre_broadcast_raspberry():
    p = Pen(RASPBERRY)
    monitor(p, 8, 6, 64, 39)
    play(p, 37, 22.5, 16)
    p.circle(57, 12, 3, fill=WHITE)                # "on air" dot
    p.label("PRE-SHOW PI")
    return p.done()


def line2_computer_sound():
    p = Pen(TEAL)
    monitor(p, 6, 7, 42, 35)
    font = ImageFont.truetype(FONT if os.path.exists(FONT) else FONT_FALLBACK, 26 * SS)
    l, t, r, b = p.d.textbbox((0, 0), "2", font=font)
    p.d.text(((24 * SS) - (r + l) / 2, (21.5 * SS) - (b + t) / 2), "2", font=font, fill=WHITE)
    speaker(p, 45, 11)
    speaker_waves(p, 59, 21, n=1, r0=6)
    p.label("LINE 2 + PC")
    return p.done()


def toggle_ftb():
    p = Pen(BLACK)
    # A screen fading to black, left to right, in a red frame.
    for i in range(40):
        v = int(255 * (1 - i / 39) ** 1.6)
        x = 14 + i * 1.1
        p.rect(x, 9, x + 1.2, 37, fill=(v, v, v))
    p.rect(12, 7, 60, 39, outline=RED, width=3.2, r=3)
    p.d.rectangle(p.s(0, 0, SIZE, SIZE), outline=RED, width=3 * SS)   # danger frame
    p.label("FTB", band=(229, 57, 53, 255))
    return p.done()


BUTTONS = [
    # (file name, Companion location, button text, drawing)
    ("p1-r0c1-main-cam.png",              "1/0/1", "Main Cam",                            main_cam),
    ("p1-r0c2-computer-pip-cam1.png",     "1/0/2", "Computer PIP Cam 1",                  computer_pip_cam1),
    ("p1-r0c3-raspberry-sound-only.png",  "1/0/3", "Raspberry Sound Only Playback video", raspberry_sound_only),
    ("p1-r1c1-main-cam-no-logo.png",      "1/1/1", "Just Main Cam No Logo",               main_cam_no_logo),
    ("p1-r1c2-left-circle-pip.png",       "1/1/2", "Left Circle PIP",                     left_circle_pip),
    ("p1-r1c3-before-start.png",          "1/1/3", "Before start",                        before_start),
    ("p1-r2c1-pre-broadcast-raspberry.png", "1/2/1", "Pre Boardcast Raspberry",           pre_broadcast_raspberry),
    ("p1-r2c2-line2-computer-sound.png",  "1/2/2", "Line 2 with Computer Sound",          line2_computer_sound),
    ("p1-r2c3-toggle-ftb.png",            "1/2/3", "Toggle FTB",                          toggle_ftb),
]


def deck_36(img):
    """72 -> 36 the way the deck does it: average each 2x2 block."""
    return img.resize((36, 36), Image.BOX)


def main():
    os.makedirs(OUT, exist_ok=True)
    icons = []
    for name, loc, text, draw in BUTTONS:
        img = draw()
        img.save(os.path.join(OUT, name))
        icons.append((name, loc, text, img))
        print(f"{name:40} {loc}  {text}")

    # Preview sheet: the 3x3 block as it sits on the page, at 72 px and at
    # the deck's 36 px, each blown up 3x with hard pixels so detail shows.
    zoom, gap = 3, 6
    cell72, cell36 = SIZE * zoom, 36 * zoom
    w = 3 * cell72 + 2 * gap + 40 + 3 * cell36 + 2 * gap
    h = 3 * cell72 + 2 * gap
    sheet = Image.new("RGB", (w + 2 * gap, h + 2 * gap), (32, 32, 32))
    for i, (_, _, _, img) in enumerate(icons):
        r, c = divmod(i, 3)
        big = img.resize((cell72, cell72), Image.NEAREST)
        sheet.paste(big, (gap + c * (cell72 + gap), gap + r * (cell72 + gap)))
        small = deck_36(img).resize((cell36, cell36), Image.NEAREST)
        x0 = gap + 3 * (cell72 + gap) + 40
        sheet.paste(small, (x0 + c * (cell36 + gap), gap + r * (cell72 + gap)))
    sheet.save(os.path.join(OUT, "preview.png"))
    print("preview.png  (left: 72 px; right: 36 px as on the 8x4 deck; both zoomed 3x)")


if __name__ == "__main__":
    main()
