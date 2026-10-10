# e210-printer

Drivers and tools for the "E210" thermal label printer (YICHIP YC3121,
vendor app QLabel), without the vendor app. Works over USB and Bluetooth.

| Folder  | Content |
|---------|---------|
| `py/`   | Python library `rmicronlib.py`, `e210print.py` (print images, text, barcodes, QR codes, receipts), `debug.py` (queries, feed, scan, UART log) |
| `c/`    | C99 single-header printer driver: `rmicron-proto.h`, `rmicron-usb.h`, `rmicron-bluetooth.h` |
| `cups/` | CUPS filter, Bluetooth backend, PPDs and Makefile |
| `doc/`  | Raw interface reference (USB, Bluetooth, UART) and the reverse engineering log |

## Python

Dependencies: `python3-pil`, `python3-usb`; `python3-qrcode` or `qrencode`
for QR codes; `python3-serial` for the UART log.

```sh
py/e210print.py --text "Hello" --size 40x30
py/e210print.py --barcode "ABC-123" --size 50x30
py/e210print.py --receipt "Line 1\nLine 2" --size receipt57
py/e210print.py label.png --dev bt:AA:BB:CC:DD:EE:FF
py/debug.py info
```

## C

Header-only, no dependencies, no allocation. Define `RMICRON_IMPLEMENTATION`
in one `.c` file before including; supply write/read callbacks for your USB
or Bluetooth stack (see the comment at the top of each header).

```sh
make -C c test
```

## CUPS

Build dependencies: `libcups2-dev`, `libcupsimage2-dev`.

```sh
cd cups
make
sudo make install
make setup usb              # or: make setup bluetooth [ADDR=AA:BB:CC:DD:EE:FF]
lp -d rmicron-e210-usb -o media=w85h57 label.pdf
```

Media sizes are named in points (`w85h57` = 30 x 20 mm); custom sizes work
with `-o media=Custom.30x20mm`. Options: `MediaType` (Gap, BlackMark, Holes,
Continuous) and `Darkness` (1-16).
