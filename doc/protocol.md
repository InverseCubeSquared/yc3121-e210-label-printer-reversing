# Protocol

The command language is the same on every transport (USB, Bluetooth SPP).
All multi-byte numbers are **little endian**. There is no framing or
checksum. Incomplete commands are discarded by the firmware, so a garbled
command does not desynchronise the stream.

Notation: `ESC` = `1B`, `FS` = `1C`, `GS` = `1D`.

## Print job

One label is printed by this sequence (the order used by the vendor app):

| # | Bytes                          | Meaning |
|---|--------------------------------|---------|
| 1 | `1C 21 n`                      | density (darkness) `n` = 0..16, default 6 |
| 2 | `1B 35 dir n`                  | horizontal offset; `dir` 0 = +, 1 = -; `n` in printer units |
| 3 | `1B 34 dir n`                  | vertical offset, same encoding |
| 4 | `1C 25 t`                      | media type, see below |
| 5 | `1B 33`                        | start of job ("Android mode" in the app) |
| 6 | `1B 29 r:u16 w:u16 h:u16 00`   | job setup: `r` = number of rows + 1, `w`/`h` = label width/height in 0.1 mm |
| 7 | `1B 19 01 len skip data...`    | one raster row, repeated for every row |
| 8 | `1B 4A 00`                     | feed 0 |
| 9 | `1B 69`                        | print and advance to the start of the next label |

Steps 1 to 4 are settings and may be left out; the printer then keeps its
current values (tested: a job of only steps 5 to 9 prints correctly). The
vendor app sends them for every label. For several copies the whole
sequence is repeated.

### Media types (`1C 25 t`)

| `t` | Media                                   |
|-----|-----------------------------------------|
| 0   | continuous (receipt roll)               |
| 1   | labels with positioning holes           |
| 2   | gap labels (default)                    |
| 3   | black mark                              |
| 4   | cable labels                            |

For continuous paper the label height `h` in step 6 is simply the image
height (rows / 8 mm); the printer stops after the last row. Not yet verified
on paper.

### Raster rows (`1B 19`)

The image is 1 bit per dot, rows top to bottom. In each row the MSB of the
first byte is the leftmost dot and **1 = black**. A full row is
`ceil(width / 8)` bytes; the image is left aligned on the label.

Each row is sent with leading and trailing zero bytes removed:

```
skip = number of leading 00 bytes of the row
data = the row without leading and trailing 00 bytes
cmd  = 1B 19 01 <len(data) + 1> <skip> <data...>
```

The length byte counts the `skip` byte plus the data. A completely white row
of `nb` bytes is `1B 19 01 01 nb`.

Examples for a 24 dot (3 byte) wide image:

| Row bytes  | Command                |
|------------|------------------------|
| `00 40 00` | `1B 19 01 02 01 40`    |
| `FF FF FF` | `1B 19 01 04 00 FF FF FF` |
| `80 00 01` | `1B 19 01 04 00 80 00 01` |
| `00 00 00` | `1B 19 01 01 03`       |

Sending full rows without trimming (`skip` = 0, `len` = bytes per row + 1)
also works (tested); the trimming only saves bandwidth.

### Width and resolution

8 dots per mm. A 30 mm wide label is 240 dots = 30 bytes per row. The
printable width is at least 48 mm (384 dots), which is the limit the vendor
app uses; `1B 17` reports 480, so the head may be 60 mm wide (unverified).

### Worked example

30x20 mm gap label, 240x160 dots, density 6:

```
1C 21 06                    density 6
1B 35 00 00                 horizontal offset 0
1B 34 00 00                 vertical offset 0
1C 25 02                    gap labels
1B 33                       start of job
1B 29 A1 00 2C 01 C8 00 00  161 rows (160 + 1), 300 x 200 (0.1 mm)
1B 19 01 1F 00 FF .. FF     row 0: 30 bytes FF (black line)
1B 19 01 01 1E              row 1: white (skip all 30 bytes)
...                         rows 2 to 159
1B 4A 00                    feed 0
1B 69                       print
```

## Motion

| Bytes     | Effect                                              |
|-----------|-----------------------------------------------------|
| `1B 69`   | advance to the start of the next label (also without an image) |
| `1B 4A n` | feed `n` dot lines, `n` = 0..255 (8 per mm)         |

## Queries

Replies echo the command bytes, followed by the data. Over USB they arrive
on the interrupt IN endpoint, over SPP on the same socket.

| Command | Reply                         | Meaning |
|---------|-------------------------------|---------|
| `1B 06` | `1B 06 s0 s1`                 | status; `00 04` when idle and ready. Bit meanings unknown |
| `1B 17` | `1B 17 CB 00 01 E0 01 00 00`  | parameters: `CB 00` = 203 dpi; `E0 01` = 480 (probably head dots); rest unknown |
| `1B 18` | `1B 18`                       | model; empty on this unit |

A scan of all two-byte commands with the prefixes `1B`, `1C`, `1D`, `10`
and `1F` found no other command that replies.

## Unsolicited bytes

The vendor app treats a single byte `E1`..`E8` sent by the printer on its
own as "print failed". The meaning of each code is not known; read the
reply channel after a job to catch them.

## Other commands

| Bytes      | Status |
|------------|--------|
| `1B 41`    | reset in the app; untested |
| `1D 49 41` | firmware version query in the app; no reply |
| `1D 49 43` | name query in the app; no reply |
| `1B 68`    | status query in the app (0 ready, 1 printing, 2 no paper, 6 lid open); no reply |
| `1B 5E`    | consumables (RFID) query in the app; no reply |
| `1B 72 00 00`, `00 1B 2A 01`, `00 1B 2A 21`, `1B 31` | defined in the app, unused, untested |
