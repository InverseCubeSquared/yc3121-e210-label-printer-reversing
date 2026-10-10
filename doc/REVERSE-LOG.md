# E210 label printer protocol

How the protocol of the "E210" thermal label printer (YICHIP YC3121 controller)
was reverse engineered, and how it works.

## The device

| Property        | Value                                                    |
|-----------------|----------------------------------------------------------|
| USB ID          | `08c5:4001`, "YICHIP / YC3121 Printer demo"              |
| Serial          | `000000000004`                                           |
| Interface       | class 7 (printer), subclass 1, protocol 2 (bidirectional)|
| Endpoints       | `0x01` bulk OUT, `0x81` interrupt IN, 64 byte packets    |
| IEEE 1284 ID    | `printer;` (no `MFG:`/`CMD:` field, so no hint at all)   |
| GET_PORT_STATUS | not answered (times out)                                 |
| Resolution      | 203 dpi (8 dots/mm)                                      |

The `usblp` kernel driver is not bound, so there is no `/dev/usb/lp*` node.
`py/rmicronlib.py` talks to the printer through pyusb (`UsbTransport`), which
needs the user to be in `plugdev`.

## Reverse engineering process

### 1. Blind probing

Status queries of the common label languages were sent and the IN endpoint
was read after each one:

| Probe                       | Bytes          | Reply |
|-----------------------------|----------------|-------|
| ESC/POS DLE EOT 1 / 4       | `10 04 01/04`  | none  |
| ESC/POS GS r 1              | `1D 72 01`     | none  |
| TSPL immediate status       | `1B 21 3F`     | none  |
| TSPL `~!T`                  | `7E 21 54 0D 0A` | none |
| Phomemo                     | `1F 11 08`     | none  |
| ZPL `~HS`                   | `7E 48 53 0D 0A` | none |

None answered, so the firmware speaks none of these. A web search for the USB
ID and product string found nothing either; "YC3121 Printer demo" is just the
default descriptor of YICHIP's chip SDK.

### 2. Getting the vendor app

The seller lists the Android app "QLabel" (package `com.rmicro.labelprinter`).
The APK (v29.0.22, 140 MB) was downloaded from the APKPure mirror
(`d.apkpure.net`; the `.com` host is behind a Cloudflare challenge) and
decompiled with jadx 1.5.6:

```
jadx -j 8 --no-res -d src qlabel.apk
```

The app contains four DEX files and bundles several printer SDKs:
`com.dothantech` (Dothan LPAPI), `com.print` (an obfuscated "YXWL" SDK, which
even carries YC3121 OTA firmware update commands), `com.yundayin` (label
template layer) and `com.rmicro.labelprinter.yp1sdk` (in-house SDK).

### 3. Finding the code path for the E210

1. `"E210"` only occurs as a model name in `com/yundayin/templet/Constant.java`
   and in UI code (`PrinterSetActivity`, `EditSetActivity`), not in any SDK.
2. The SDK is chosen when connecting, by the Bluetooth device name, in
   `ConnectionListActivity` (item click) and `SuperActivity` (auto reconnect):
   names starting with `YP1` (4 chars), `E100`, `GG-`, `EQ11`, `YP10`, **`E2`**,
   `LT`, `DM1`, `E50W` or `T5` go to `connectPrinterByAddressForYP1()`, i.e. the
   in-house `yp1sdk`. `YP11`/`YP30` go to the YXWL SDK, everything else to
   Dothan LPAPI.
3. In `com/rmicro/labelprinter/bluetooth/LPBTService.java`, `drawBitmap()`
   calls `printImage()` for yp1sdk connections. `printImage()` together with
   `setPrintParams()` is the complete print job.
4. All command bytes are built in `yp1sdk/util/PrinterHelper.java`;
   `WriteData()` hands them to `BluetoothSdkManager.print()` which writes them
   unchanged to an SPP socket (`yp1sdk/service/BtService.java`,
   `ConnectedThread.write()`). There is no packet framing or checksum.

`yp1sdk/BtCmdService.java` also contains CPCL demo code (STO courier
waybills); this is leftover SDK sample code and not used for the E210.

### 4. Verification over USB

The yp1sdk query commands were sent over USB and the printer answered (see
"Replies" below), confirming the language. A job built exactly like
`printImage()` then printed a 30x20 mm test frame on the first try. The
Bluetooth byte stream works unchanged over USB.

### 5. Second round: command scan and motion commands

Every two-byte command `<prefix> xx` for the prefixes `1B`, `1C`, `1D`, `10`
and `1F` (1280 commands, the known print/feed commands excluded) was sent,
each followed by the status query `1B 06` as a sync marker (`py/debug.py scan`).
Only `1B 06`, `1B 17` and `1B 18` produce a reply. No command ever swallowed
the following sync bytes as arguments, so the firmware drops incomplete
commands (inter-byte timeout or restart on `1B`); blind probing is safe.

Confirmed on paper:

| Bytes     | Effect                                             |
|-----------|----------------------------------------------------|
| `1B 69`   | advances to the start of the next label (no data needed) |
| `1B 4A n` | feeds `n` dot lines (`n` = 80 -> 10 mm)            |

Further findings in the app:

- **Error bytes.** `LPBTService.initSDKManager()` treats any single
  unsolicited byte `0xE1`..`0xE8` from the printer as "print failed". The
  meaning of each code is not in the app.
- **Label formats.** The E210 has no built-in size list; the user enters
  width and height. The editor offers the paper types gap (default), black
  mark, holes and continuous, plus a "cable label" mode (`lineLabel`, tail
  direction and length). `printImage()` caps the rendered width at 48 mm.
- **`0x1D` vs `BoolPtg.sid`.** jadx printed the byte `0x1D` as the Apache POI
  constant `BoolPtg.sid`, so the name/version queries are `1D 49 43` /
  `1D 49 41` (`GS I`), not `1F ...`. The firmware does not answer either.
- **YXWL SDK.** The `com.print` SDK (YP11/YP30 models) uses a different
  `10 FF xx` command set whose payloads are encrypted by
  `libPrinterNative.so` (`NativeManage.b()`). The E210 does not use it; dead end.
- **Firmware.** The APK contains no printer firmware images.

### 6. Debug UART

The board has a UART header (3.3 V TTL). Settings: **~266667 baud** (clean
between 260000 and 272000; probably 48 MHz / 180), 8N1. It was found by
sweeping baud rates while triggering output over USB; at 256000 the first
readable fragments appeared, then a fine sweep in 4000 baud steps.

It is a **log output only**:

- At boot it logs the Bluetooth setup (classic and BLE name `E210`, the GATT
  services and handles), a dump of the Bluetooth nvram (contains addresses and
  link keys of paired phones; treat a capture as sensitive),
  `g_VoltagCalTemp:<n>` and `g_paperLPSe :<n>` (probably battery calibration
  and paper sensor level).
- On every USB transfer it logs
  `s_PrintParameters.m_PrHeadHottime:905` and `...m_CurFreeTime:700`.
- While printing it logs `F....` lines; feed button and lid produce short
  lines such as `x---` / `1---`.

The RX pin did not react to anything: binary queries (`1B 06`), text commands
(`help`, `AT`, ...) and `1B 69` (no paper movement) at 9600, 115200 and
266667 baud. Printing over the UART is therefore not possible with this
firmware.

### 7. Bluetooth

The YC3121 has the radio built in; the printer is dual-mode and named `E210`.

- **Classic, SPP** (UUID `0x1101`, RFCOMM channel 1). No pairing needed.
  The same byte stream as over USB works unchanged: status query, parameter
  query and a full label print were tested from Linux with a plain RFCOMM
  socket (`py/e210print.py --dev bt:<address>`).
- **BLE.** Advertised service UUIDs: `FEE7`,
  `ffcacade-afde-cade-defa-cade00000000`, `0x0001`, `0x0003`, `0x0100`. The
  boot log names services with tx/rx/flow-control characteristics (`FFF0`,
  `FEE7`, a vendor one tagged `4953`). The GATT table could not be read:
  BlueZ 5.64 always connects this dual-mode device over BR/EDR
  (`br-connection-profile-unavailable`) and has no per-device LE bearer
  option. TODO: probe this with a phone app (nRF Connect) or an ESP32.

### 8. OLED display

No command for the SSD1306 display was found: none in the app, none in the
command scan, nothing on the UART. The display content is generated by the
firmware from fixed data.

### 9. Open points

- Printable width: 48 mm (app limit, `HEAD_DOTS = 384`) or 60 mm (`1B 17`
  reports 480). Check with `py/e210print.py --ruler --size receipt57 --width 60
  --head-mm 60` on a 57 mm roll.
- Continuous (receipt) paper is implemented as paper type 0 with the label
  height taken from the image, but not yet tested on paper.
- Status byte meanings and the `E1`..`E8` error codes.

## Protocol

All multi-byte numbers are little endian. Commands are sent back to back with
no delays.

### Print job sequence

This is the order used by `LPBTService.setPrintParams()` and `printImage()`:

| # | Command                  | Bytes                         | Notes |
|---|--------------------------|-------------------------------|-------|
| 1 | Density                  | `1C 21 n`                     | `n` = 0..16, app default 6 |
| 2 | Horizontal offset        | `1B 35 dir n`                 | `dir` 0 = positive, 1 = negative; `n` in app units |
| 3 | Vertical offset          | `1B 34 dir n`                 | same encoding |
| 4 | Paper type               | `1C 25 t`                     | see table below |
| 5 | "Android mode"           | `1B 33`                       | `SET_PRINTER_FOR_ANDROID`, no arguments |
| 6 | Label setup              | `1B 29 rows:u16 w:u16 h:u16 0`| `rows` = image height + 1, `w`/`h` = label size in 0.1 mm |
| 7 | Raster rows              | `1B 19 01 len skip data...`   | one command per pixel row, see below |
| 8 | Feed 0                   | `1B 4A 00`                    | `SET_PRINTER_PAGE_RUN_NEXT_FIX` |
| 9 | Print / next label       | `1B 69`                       | `SET_PRINTER_PAGE_RUN_NEXT`; prints and advances to the next label |

Paper types (from the log message in `LPBTService.setPrinterPageType()`):

| `t` | Media           |
|-----|-----------------|
| 0   | continuous      |
| 1   | positioning holes |
| 2   | gap labels      |
| 3   | black mark      |
| 4   | cable labels    |

For several copies the app repeats the whole sequence.

### Raster row encoding

The image is 1 bit per pixel, MSB = leftmost pixel, **1 = black**, `ceil(width/8)`
bytes per row. Rows are sent top to bottom, left aligned (x = 0 is the left
edge of the label).

Each row is trimmed before sending (`PrinterHelper.getTargetPrintData()`):

```
skip = number of leading 0x00 bytes
data = row[skip : nb - trailing_zero_bytes]
cmd  = 1B 19 01 (len(data) + 1) skip data...
```

The length byte counts the `skip` byte plus the data. A blank row is therefore
`1B 19 01 01 nb` (skip the whole row, no data). Since a row has at most 60 bytes
at 480 dots, the single length byte is always enough.

Example, 24 dot wide row with one black pixel at x = 9:

```
row bytes  00 40 00
skip = 1, data = 40
command    1B 19 01 02 01 40
```

### Image size

The app renders the label at `min(label_width_mm, 48) * 8` dots wide and
scales the height proportionally (`LPBTService.printImage()`). The label setup
command still gets the real label size in 0.1 mm.

The app dithers with its own error-diffusion routine (`PrinterHelper.sDither()`)

### Complete minimal example

A 1 mm high, 1 mm wide all-black label (8x8 dots), gap paper, density 6:

```
1C 21 06                       density 6
1B 35 00 00                    h offset 0
1B 34 00 00                    v offset 0
1C 25 02                       gap labels
1B 33                          android mode
1B 29 09 00 0A 00 0A 00 00     9 rows, 10 x 10 (0.1 mm)
1B 19 01 02 00 FF              row 0  (x8)
...
1B 4A 00                       feed 0
1B 69                          print
```

### Query commands and replies

All replies arrive on the interrupt IN endpoint `0x81` and echo the command
bytes first.

| Command | Bytes      | Name in app            | Reply observed            |
|---------|------------|------------------------|---------------------------|
| Status  | `1B 06`    | `GET_PRINTER_STATUS`   | `1B 06 00 04`             |
| Params  | `1B 17`    | `GET_PRINTER_PARAM`    | `1B 17 CB 00 01 E0 01 00 00` |
| Model   | `1B 18`    | `GET_PRINTER_MODEL`    | `1B 18` (empty)           |
| Status  | `1B 68`    | `getStatus()`          | none                      |
| Name    | `1D 49 43` | `getPrintName()`       | none                      |
| Version | `1D 49 41` | `getPrintVersion()`    | none                      |
| Info    | `1B 5E`    | `GET_HAOCAI_INFOS`     | none                      |

Interpretation, as far as it is known:

- **`1B 17`**: `CB 00` = 203 (dpi). `E0 01` = 480, most likely the head width in
  dots (60 mm). The remaining bytes are unknown.
- **`1B 06`**: the status byte was `00 04` both idle and right after a
  successful print, so it does not obviously flag "busy". The app's decoder
  (`PrinterHelper.getRDPrinterStatus()`) checks bits 0..5 but overwrites its
  result so it always returns 10 or 11, which makes it useless as a reference.
  The meaning of the bits is not confirmed.
- `getStatus()` (`1B 68`) would return 0 = ready, 1 = printing, 2 = no paper,
  6 = lid open according to `BtCmdService.getStatus()`, but this firmware does
  not answer it over USB.

### Other commands found but not used

These are defined in `PrinterHelper` but not part of the E210 print path;
untested:

| Bytes          | Name in app               |
|----------------|---------------------------|
| `1B 41`        | reset (`BtCmdService.reSet()`) |
| `1B 31`        | `PRINT_LINESPACE`         |
| `1B 72 00 00`  | `SET_PRINTER_IMAGE`       |
| `00 1B 2A 01` / `00 1B 2A 21` | `IMAGECMD` / `IMAGECMD24` (ESC/POS style bit image) |
| `0D` / `0A`    | `ENTER_PRINT` / `WRAP_PRINT` |
