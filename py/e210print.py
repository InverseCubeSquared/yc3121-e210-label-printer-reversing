#!/usr/bin/env python3
"""e210print.py - print on a YC3121 "E210" label printer over USB,
without CUPS. Uses rmicronlib.py.

  e210print.py label.png
  e210print.py notes.txt --size receipt57
  e210print.py --text "Hello\\nWorld" --size 40x30
  e210print.py --barcode "ABC-12345" --size 50x30
  e210print.py --qrcode "https://example.org"
  e210print.py --receipt "Line 1\\nLine 2" --size receipt57
  e210print.py --test --density 8

Needs Pillow (python3-pil) and pyusb (python3-usb); --qrcode also needs
python3-qrcode or qrencode.
"""
import argparse
import sys

import rmicronlib as rm


def read_text(path):
    """Contents of a plain text file, or None if it is binary."""
    with open(path, "rb") as f:
        data = f.read()
    if b"\0" in data:
        return None
    for enc in ("utf-8-sig", "cp1252"):
        try:
            text = data.decode(enc)
        except UnicodeDecodeError:
            continue
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        if all(c.isprintable() or c in "\n\t\f" for c in text):
            return text.expandtabs(4).replace("\f", "\n")
    return None


def is_image(path):
    try:
        with rm.Image.open(path):
            return True
    except rm.Image.UnidentifiedImageError:
        return False


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("file", nargs="?",
                   help="image file (PNG, JPEG, ...) or plain text file, "
                        "printed like --receipt")
    src = p.add_argument_group("content (pick one)")
    src.add_argument("--text", help="centred text; \\n starts a new line")
    src.add_argument("--receipt", help="word-wrapped text for continuous "
                                       "paper; '-' reads stdin")
    src.add_argument("--barcode", help="Code 128 barcode of this string")
    src.add_argument("--qrcode", help="QR code of this string")
    src.add_argument("--test", action="store_true", help="test pattern")
    src.add_argument("--ruler", action="store_true",
                     help="mm ruler across the full width (head width check)")
    m = p.add_argument_group("media")
    m.add_argument("--size", default="30x20",
                   help="preset (%s) or WxH in mm; H 0 = continuous"
                        % ", ".join(rm.PRESETS))
    m.add_argument("--width", type=float, help="override width, mm")
    m.add_argument("--height", type=float,
                   help="override height, mm (0 = continuous)")
    m.add_argument("--paper", choices=list(rm.PAPER),
                   help="media type (default: from preset, else gap)")
    o = p.add_argument_group("output")
    o.add_argument("--density", type=int, default=6, help="darkness 0-16")
    o.add_argument("--copies", type=int, default=1)
    o.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270])
    o.add_argument("--dither", action="store_true",
                   help="dither greys instead of thresholding at 50%%")
    o.add_argument("--font", help="TrueType font for --text/--receipt")
    o.add_argument("--font-size", type=int, default=22,
                   help="--receipt font size in dots")
    o.add_argument("--feed", type=float, default=0,
                   help="extra feed after printing, mm (continuous paper)")
    o.add_argument("--dev", help="device: default USB, '-' = raw bytes to "
                                 "stdout, bt:AA:BB:.. = Bluetooth, or a path")
    o.add_argument("--preview", help="also save the 1-bit image to this PNG")
    o.add_argument("--head-mm", type=float, default=rm.HEAD_DOTS / 8,
                   help="printable width limit, mm (default 48)")
    a = p.parse_args()

    rm.HEAD_DOTS = rm.mm(a.head_mm)

    # media
    if a.size in rm.PRESETS:
        wmm, hmm, paper = rm.PRESETS[a.size]
    else:
        try:
            wmm, hmm = (float(v) for v in a.size.lower().split("x"))
        except ValueError:
            p.error("--size: unknown preset or not WxH: %s" % a.size)
        hmm, paper = hmm or None, "gap" if hmm else "continuous"
    wmm = a.width or wmm
    if a.height is not None:
        hmm = a.height or None
    paper = a.paper or paper
    w = min(rm.mm(wmm), rm.HEAD_DOTS)
    h = rm.mm(hmm) if hmm else None
    if h is None and (a.text is not None or a.barcode is not None or
                      a.qrcode is not None or a.test or a.ruler):
        h = rm.mm(wmm if a.qrcode is not None else 20 if a.ruler else 30)

    # content
    try:
        if a.test:
            img = rm.test_image(w, h)
        elif a.ruler:
            img = rm.ruler_image(w, h)
        elif a.text is not None:
            img = rm.text_image(a.text.replace("\\n", "\n"), w, h, a.font)
        elif a.receipt is not None:
            text = sys.stdin.read() if a.receipt == "-" else a.receipt
            img = rm.receipt_image(text.replace("\\n", "\n"), w,
                                   a.font_size, a.font)
            if h is not None:                       # fixed label: crop/pad
                img = img.crop((0, 0, w, h))
        elif a.file and not is_image(a.file):
            text = read_text(a.file)
            if text is None:
                sys.exit("%s: neither an image nor a text file" % a.file)
            img = rm.receipt_image(text.rstrip("\n"), w, a.font_size, a.font)
            if h is not None:
                img = img.crop((0, 0, w, h))
        elif a.barcode is not None:
            img = rm.barcode_image(a.barcode, w, h, a.font)
        elif a.qrcode is not None:
            img = rm.qr_image(a.qrcode, w, h)
        elif a.file:
            img = rm.file_image(a.file, w, h, a.rotate)
        else:
            p.error("give an image file, --text, --receipt, --barcode, "
                    "--qrcode, --test or --ruler")
    except (ValueError, RuntimeError, OSError) as e:
        sys.exit(str(e))
    if a.preview:
        img.convert("1", dither=rm.Image.FLOYDSTEINBERG if a.dither
                    else rm.Image.NONE).save(a.preview)

    try:
        with rm.E210(a.dev) as pr:
            n = pr.print_image(img, wmm, hmm, paper, a.density, a.copies,
                               dither=a.dither)
            if a.feed:
                pr.feed(rm.mm(a.feed))
    except (rm.PrinterError, ValueError) as e:
        sys.exit("printer: %s" % e)
    if a.dev != "-":
        print("sent %d bytes (%dx%d dots, %s) " % (n, img.width, img.height,
                                                   paper), file=sys.stderr)


if __name__ == "__main__":
    main()
