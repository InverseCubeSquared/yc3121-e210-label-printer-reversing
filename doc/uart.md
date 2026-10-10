# Debug UART

The printer board has a UART header driven by the YC3121. It is a
**debug log output**; the firmware does not accept commands or print data
on it.

## Electrical and line settings

| Property  | Value                                              |
|-----------|----------------------------------------------------|
| Level     | 3.3 V TTL. Do not use an RS-232 (+-12 V) adapter   |
| Baud rate | ~266667 (works from 260000 to 272000; probably 48 MHz / 180) |
| Format    | 8N1                                                |
| Pins used | GND, printer TX (-> adapter RX). Leave VCC unconnected |

266667 baud is not a standard rate. Adapters with a fractional divider
(CH340/CH341, CP2102N, FT232R) can produce it; check the error on others.

## Reading the log

```sh
python3 -m serial.tools.miniterm /dev/ttyUSB0 266667
```

or

```python
import serial
s = serial.Serial("/dev/ttyUSB0", 266667, timeout=0.1)
while True:
    line = s.readline()
    if line:
        print(line.decode("latin-1").rstrip())
```

## What it logs

### Boot

```
Star..0.....
Star..1.....
bt ble demo
BT_Init end
SetBleName_suc ble name:E210
SetbtName_suc
SetBleAddr_suc
SetBtAddr_suc
set confirmkey mode success
set COD sucess
set lpm mode sucess
delete service sucess
add service sucess,handle=000c
add Characteristic tx sucess,handle=000e
add Characteristic rx sucess;handle=0011
add Characteristic flow_ctrl sucess;handle=0013
add service sucess,handle=0015
add Characteristic tx sucess,Wr_handle_4953=0017
...
BT_SetLEAdvData sucess
BT_SetLEScanData sucess
SetVisibility sucess
bt version=100c
set nvram success:
0x02 0x00 ... (Bluetooth pairing records)
g_VoltagCalTemp:4246
g_paperLPSe :224---
```

- The full GATT setup is listed; see [bluetooth.md](bluetooth.md).
- `set nvram` dumps the Bluetooth pairing records, including addresses and
  link keys of paired phones. Treat captures as sensitive.
- `g_VoltagCalTemp` is probably a battery voltage calibration value (mV),
  `g_paperLPSe` probably the paper sensor reading.

### Runtime

| Trigger                       | Log output                               |
|-------------------------------|------------------------------------------|
| any data received over USB    | `s_PrintParameters.m_PrHeadHottime:905` and `s_PrintParameters.m_CurFreeTime:700` |
| printing                      | `F....` lines                            |
| feed button, lid              | short lines such as `x---`, `1---`       |
| idle                          | nothing                                  |

`m_PrHeadHottime` is probably the print head heating time per line and
`m_CurFreeTime` the pause between lines (both in microseconds); they change
slightly between runs (900/905, 696/700).

## Input

The RX pin was tested with:

| Sent                                              | Baud rates |
|---------------------------------------------------|------------|
| `1B 06`, `1B 17` (queries)                        | 9600, 19200, 38400, 57600, 115200, 230400, 250000, 266667, 460800, 921600, 1000000 |
| `help`, `?`, `AT`, `AT+VER`, `ls`, `version`, empty line, `1B 06` | 9600, 115200, 266667 |
| `1B 69` (next label)                              | 9600, 115200, 266667 |

There was no reply on the UART or on USB, and no paper movement. Printing over the UART is not possible with this firmware;
use Bluetooth SPP for microcontroller projects ([bluetooth.md](bluetooth.md)).
