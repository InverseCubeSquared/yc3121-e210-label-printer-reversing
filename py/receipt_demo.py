#!/usr/bin/env python3
"""receipt_demo.py - Deponia-themed demo receipt for the E210 (48 mm
continuous paper). Renders a PNG, print it with e210print.py:

  receipt_demo.py receipt.png
  e210print.py receipt.png --size receipt57 --feed 15
"""
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmicronlib as rm
from PIL import Image, ImageDraw

W = rm.HEAD_DOTS
MARGIN = 6

SHOP = "Kuvaq\nKrempelladen"
ADDRESS = ["M\u00fcllhalde 7, Schrottgasse", "00042 Kuvaq, Deponia"]
ITEMS = [
    ("Plasmaaustauschfluppidiwuppmammutfellrelais", 13.37),
    ("Elysium-Fahrkarte (garantiert echt)", 99.99),
    ("Organon-Uniformknopf, poliert", 4.20),
    ("Rufus' Notfall-Bastelset", 7.77),
    ("Klebeband, halb abgerollt", 0.50),
    ("Doc's Ersatzgehirn im Glas", 23.00),
    ("Schrottplatz-Souvenir \"Gru\u00df aus Kuvaq\"", 2.99),
]

body = rm.load_font(20, rm.MONO)
head = rm.load_font(46)
probe = ImageDraw.Draw(Image.new("L", (1, 1)))


def wrap(text, font, width):
    """Word wrap; words longer than a line are split by character."""
    lines, line = [], ""
    for word in text.split(" "):
        t = word if not line else line + " " + word
        if probe.textlength(t, font=font) <= width:
            line = t
            continue
        if line:
            lines.append(line)
        line = ""
        for ch in word:
            if probe.textlength(line + ch, font=font) > width:
                lines.append(line)
                line = ""
            line += ch
    lines.append(line)
    return lines


class Receipt:
    def __init__(self):
        self.img = Image.new("L", (W, 4000), 255)
        self.d = ImageDraw.Draw(self.img)
        self.y = MARGIN

    def text(self, s, font=body, align="left", right=None):
        b = self.d.textbbox((0, 0), s, font=font)
        tw = b[2] - b[0]
        x = {"left": MARGIN, "center": (W - tw) // 2,
             "right": W - MARGIN - tw}[align] - b[0]
        self.d.text((x, self.y), s, font=font, fill=0)
        if right is not None:
            rb = self.d.textbbox((0, 0), right, font=font)
            self.d.text((W - MARGIN - rb[2], self.y), right, font=font,
                        fill=0)
        asc, desc = font.getmetrics()
        self.y += asc + desc + 2

    def sep(self, char="="):
        n = int((W - 2 * MARGIN) // probe.textlength(char, font=body))
        self.text(char * n, align="center")

    def gap(self, dots):
        self.y += dots

    def paste(self, im):
        self.img.paste(im, ((W - im.width) // 2, self.y))
        self.y += im.height

    def done(self):
        return self.img.crop((0, 0, W, self.y + MARGIN))


nr = "".join(random.choice("0123456789") for _ in range(12))
r = Receipt()
for line in SHOP.split("\n"):
    r.text(line, font=head, align="center")
r.gap(6)
r.text("Bon-Nr. " + nr, align="center")
r.gap(6)
for line in ADDRESS:
    r.text(line, align="center")
r.gap(6)
r.text(time.strftime("%d.%m.%Y  %H:%M:%S"), align="center")
r.gap(4)
r.sep("-")

price_w = probe.textlength(" 000,00", font=body)
total = 0.0
for name, price in ITEMS:
    total += price
    lines = wrap(name, body, W - 2 * MARGIN - price_w)
    for line in lines[:-1]:
        r.text(line)
    r.text(lines[-1], right=("%.2f" % price).replace(".", ","))
    r.gap(2)

r.sep("=")
big = rm.load_font(28)
r.text("SUMME", font=big, right="%s EUR" % ("%.2f" % total).replace(".", ","))
r.sep("=")
r.gap(8)
r.paste(rm.barcode_image(nr, W - 2 * MARGIN, rm.mm(14)))
r.gap(8)
r.text("Danke f\u00fcr Ihren Einkauf!", align="center")
r.text("Kein Umtausch. Kein Elysium.", align="center")

img = r.done()
img.save(sys.argv[1] if len(sys.argv) > 1 else "receipt.png")
