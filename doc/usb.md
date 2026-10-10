# USB

## Descriptors

| Property        | Value                                                |
|-----------------|------------------------------------------------------|
| VID:PID         | `08c5:4001`                                          |
| Strings         | manufacturer `YICHIP`, product `YC3121 Printer demo`, serial `000000000004` |
| Interface 0     | class 7 (printer), subclass 1, protocol 2 (bidirectional) |
| EP `0x01`       | bulk OUT, 64 byte packets: commands and print data   |
| EP `0x81`       | interrupt IN, 64 byte packets: replies               |
| IEEE 1284 ID    | `printer;` (class request `GET_DEVICE_ID`)           |
| `GET_PORT_STATUS` | not answered                                       |

The product string is the YICHIP SDK default, so the device can not be
identified by name; match on VID:PID.

## Access on Linux

- The kernel `usblp` driver may bind and create `/dev/usb/lp0`. That node is
  fine for writing a job (`cat job.bin > /dev/usb/lp0`), but replies are
  easier to get with libusb.
- With libusb the interface must be claimed (detach `usblp` first if bound).
  Users in `plugdev` usually have access to `/dev/bus/usb/...`; otherwise
  add a udev rule:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="08c5", ATTRS{idProduct}=="4001", MODE="0660", GROUP="plugdev"
```

- A CUPS queue on the same device can grab it while printing; do not keep
  one configured for this printer.

## Sending and receiving

Write the byte stream to EP `0x01` in chunks of any size (4096 is fine).
Read replies from EP `0x81`; a reply may span several 64 byte transfers, so
read until a short timeout (e.g. 300 ms) expires.

## Examples

### Python, pyusb: status and parameters

```python
import usb.core, usb.util

dev = usb.core.find(idVendor=0x08C5, idProduct=0x4001)
if dev.is_kernel_driver_active(0):
    dev.detach_kernel_driver(0)
usb.util.claim_interface(dev, 0)

def query(cmd):
    dev.write(0x01, cmd)
    return bytes(dev.read(0x81, 64, timeout=500))

print(query(b"\x1b\x06").hex(" "))      # 1b 06 00 04
print(query(b"\x1b\x17").hex(" "))      # 1b 17 cb 00 01 e0 01 00 00
usb.util.release_interface(dev, 0)
```

### Python, pyusb: print a label

A 30x20 mm label with a frame, built from scratch (Pillow only for drawing):

```python
import struct, usb.core, usb.util
from PIL import Image, ImageDraw

w, h = 240, 160                                   # 30 x 20 mm at 8 dots/mm
img = Image.new("1", (w, h), 1)                   # 1 = white in Pillow
ImageDraw.Draw(img).rectangle((0, 0, w - 1, h - 1), outline=0)
raw = bytes(b ^ 0xFF for b in img.tobytes())      # printer: 1 = black
nb = w // 8

job = b"\x1c\x21\x06" + b"\x1c\x25\x02" + b"\x1b\x33"
job += b"\x1b\x29" + struct.pack("<HHHB", h + 1, 300, 200, 0)
for y in range(h):
    job += b"\x1b\x19\x01" + bytes([nb + 1, 0]) + raw[y * nb:(y + 1) * nb]
job += b"\x1b\x4a\x00" + b"\x1b\x69"

dev = usb.core.find(idVendor=0x08C5, idProduct=0x4001)
usb.util.claim_interface(dev, 0)
dev.write(0x01, job, timeout=10000)
usb.util.release_interface(dev, 0)
```

### Shell: feed and next label

With `usblp` bound (`/dev/usb/lp0`):

```sh
printf '\x1b\x69' > /dev/usb/lp0          # advance to the next label
printf '\x1b\x4a\x50' > /dev/usb/lp0      # feed 80 dot lines = 10 mm
```

### C, libusb-1.0: status query

```c
#include <stdio.h>
#include <libusb-1.0/libusb.h>

int main(void)
{
    libusb_context *ctx;
    libusb_init(&ctx);
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, 0x08c5, 0x4001);
    if (!h) { fprintf(stderr, "printer not found\n"); return 1; }
    libusb_set_auto_detach_kernel_driver(h, 1);
    libusb_claim_interface(h, 0);

    unsigned char cmd[] = { 0x1b, 0x06 }, rep[64];
    int n;
    libusb_bulk_transfer(h, 0x01, cmd, sizeof cmd, &n, 1000);
    // EP 0x81 is an interrupt endpoint
    if (libusb_interrupt_transfer(h, 0x81, rep, sizeof rep, &n, 1000) == 0)
        for (int i = 0; i < n; i++)
            printf("%02x ", rep[i]);
    printf("\n");

    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
```

Build: `gcc -std=c99 -o e210status e210status.c -lusb-1.0`

## Microcontrollers

Any USB host capable MCU can print by acting as a USB host for a printer
class device and writing the same bytes to EP `0x01`: for example
ESP32-S2/S3 (ESP-IDF USB host), RP2040/RP2350 (TinyUSB host or PIO-USB) or
an Arduino with a USB host shield. Bluetooth SPP is usually simpler, see
[bluetooth.md](bluetooth.md).
