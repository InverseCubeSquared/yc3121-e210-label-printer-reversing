# E210 label printer: raw interface documentation

Reference for driving the "E210" thermal label printer (YICHIP YC3121
controller, QLabel app) directly, without any library or driver. Everything
here was found by reverse engineering; see `REVERSE-LOG.md` for how.

| File                         | Content                                         |
|------------------------------|-------------------------------------------------|
| [protocol.md](protocol.md)   | Command set, print job, raster encoding, replies |
| [usb.md](usb.md)             | USB interface, endpoints, raw examples          |
| [bluetooth.md](bluetooth.md) | Classic SPP and BLE, raw examples, microcontrollers |
| [uart.md](uart.md)           | Debug UART on the board (log output only)       |

## Device summary

| Property        | Value                                          |
|-----------------|------------------------------------------------|
| Controller      | YICHIP YC3121 (MCU with Bluetooth radio)       |
| Resolution      | 203 dpi, 8 dots per mm                         |
| Print width     | 48 mm (384 dots) confirmed; 60 mm (480 dots) possible, unverified |
| Media           | gap labels, black mark, holes, continuous      |
| Interfaces      | USB printer class, Bluetooth SPP, BLE, debug UART (TX only) |
| Language        | own binary command set (not ESC/POS, TSPL, ZPL, CPCL) |

The same byte stream works unchanged over USB and Bluetooth SPP. There is no
framing, checksum or handshake; commands are sent back to back.

## Quick start

Print a 30x20 mm label with a single black bar, using only standard tools.
The job (hex) is:

```
1C 21 06  1B 35 00 00  1B 34 00 00  1C 25 02  1B 33
1B 29 A1 00 2C 01 C8 00 00
... 160 rows of 1B 19 01 ...
1B 4A 00  1B 69
```

[protocol.md](protocol.md) explains each part; [usb.md](usb.md) and
[bluetooth.md](bluetooth.md) show how to send it.

## What works where

| Feature                     | USB | Bluetooth SPP | BLE     | UART |
|-----------------------------|-----|---------------|---------|------|
| Print                       | yes | yes           | unknown | no   |
| Feed / next label           | yes | yes           | unknown | no   |
| Status / parameter queries  | yes | yes           | unknown | no   |
| Debug log                   | no  | no            | no      | yes (output only) |

## Not available

- No command to change the OLED (SSD1306) display content.
- No firmware version or name query answers (`1D 49 41` / `1D 49 43`).
- No print or command input over the UART.
