# e210-printer: Python driver

Python driver for the "E210" thermal label printer (YC3121), over USB or
Bluetooth, without the vendor app.

| File | Content |
|------|---------|
| `rmicronlib.py` | Library: transports, rendering helpers, job encoding |
| `e210print.py`  | Command line printing: images, text, barcodes, QR codes, receipts |
| `debug.py`      | Queries and control without printing (status, feed, paper type, UART log) |
| `receipt_demo.py` | Example: renders a full width shop receipt with barcode to a PNG |

## Dependencies

- `python3-pil` and `python3-usb` (required)
- `python3-qrcode` or the `qrencode` binary for `--qrcode`
- `python3-serial` for `debug.py log`

USB access without root needs a udev rule for `08c5:4001`, see
`doc/usb.md`.

## Labels

Labels are gap media by default. Pick a preset with `--size` or give
`WxH` in mm:

```sh
./e210print.py --text "Hello\nWorld" --size 40x30
./e210print.py --barcode "ABC-123" --size 50x30
./e210print.py --qrcode "https://example.org" --size 40x40
./e210print.py label.png --size 50x30 --dither
./e210print.py --test --size 30x20 --density 8
```

Presets: `15x30 20x10 25x15 30x15 30x20 30x40 40x20 40x30 40x40 40x60
48x30 50x20 50x25 50x30 50x40 50x50 50x80` (see `PRESETS` in
`rmicronlib.py`). Images are scaled to the label width and centred. Labels
wider than the print head (48 mm by default) are printed on the left
48 mm only.

For black mark or punched media add `--paper blackmark` or `--paper holes`.
`debug.py next` advances to the next label if the printer lost the gap.

## Receipt paper (full width)

Continuous 57/58 mm till rolls use the `receipt57` preset, which prints
across the full head width of 48 mm (384 dots). The length follows the
content.

```sh
./e210print.py --receipt "Line 1\nLine 2" --size receipt57 --feed 10
cat bon.txt | ./e210print.py --receipt - --size receipt57 --font-size 26
./e210print.py bon.txt --size receipt57
./e210print.py photo.png --size receipt57 --dither
./e210print.py --ruler --size receipt57
```

`--receipt` word-wraps the text to the width. A text file given in place
of an image is printed the same way (UTF-8 or cp1252; binary files are
rejected). `--feed` adds blank paper
after the job so the print clears the tear-off edge. `--ruler` prints a mm
scale across the full width to check the head width and alignment.

48 mm is the hardware maximum; wider jobs (for example
`--head-mm 56 --width 56`, 448 dots per row) do not print correctly.
`--head-mm` is only useful to lower the limit.

## Other options

| Option | Meaning |
|--------|---------|
| `--density N` | Darkness 0-16 (default 6) |
| `--copies N` | Print the job N times |
| `--rotate 90` | Rotate image files (0, 90, 180, 270) |
| `--font FILE` | TrueType font for `--text` and `--receipt` |
| `--preview out.png` | Also save the 1-bit image that is sent |
| `--dev DEV` | `bt:AA:BB:CC:DD:EE:FF` for Bluetooth, a device path such as `/dev/usb/lp0`, or `-` for raw bytes on stdout |

## Debugging

```sh
./debug.py info               # status, parameters (dpi), model
./debug.py feed 10            # feed 10 mm
./debug.py paper continuous   # set media type
./debug.py listen             # show error codes sent by the printer
```

## Library use

```python
from rmicronlib import E210, text_image

with E210() as p:                         # first E210 on USB
    img = text_image("Hello", *p.size_dots(40, 30))
    p.print_image(img, 40, 30)
```

`print_image(img, width_mm, height_mm=None, paper="gap", density=6, ...)`
takes a Pillow "L" image in dots (8 dots per mm, 0 = black);
`height_mm=None` means continuous paper.
