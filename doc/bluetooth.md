# Bluetooth

The YC3121 has the radio built in. The printer is dual-mode (classic and
BLE) and advertises the name `E210`, class of device `0x040680` (printer).

## Classic: Serial Port Profile (SPP)

| Property      | Value                                  |
|---------------|----------------------------------------|
| Profile       | SPP, UUID `00001101-0000-1000-8000-00805F9B34FB` |
| RFCOMM channel| 1                                      |
| Pairing       | not required                           |
| Data          | the same byte stream as over USB ([protocol.md](protocol.md)) |
| Replies       | come back on the same RFCOMM link      |

Tested from Linux: status query, parameter query and full label prints.
This is the transport the vendor app uses.

### Find the printer (Linux)

```sh
bluetoothctl --timeout 10 scan on      # look for "E210"
sdptool browse AA:BB:CC:DD:EE:FF       # optional: confirm SPP / channel 1
```

### Python: raw RFCOMM socket

```python
import socket

s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
s.connect(("AA:BB:CC:DD:EE:FF", 1))
s.settimeout(1.0)

s.sendall(b"\x1b\x06")
print(s.recv(64).hex(" "))             # 1b 06 00 04

s.sendall(b"\x1b\x69")                 # advance to the next label
s.close()
```

A print job is sent the same way: build it as in [usb.md](usb.md) and pass
it to `sendall()`.

### Shell: rfcomm device node

```sh
sudo rfcomm bind 0 AA:BB:CC:DD:EE:FF 1     # creates /dev/rfcomm0
printf '\x1b\x4a\x50' > /dev/rfcomm0       # feed 10 mm
cat job.bin > /dev/rfcomm0                 # send a prepared job
sudo rfcomm release 0
```

### ESP32 (Arduino, classic Bluetooth)

The original ESP32 (not S2/S3/C3, which lack classic Bluetooth) can connect
as SPP master. This sketch prints a 30x20 mm label with a frame and a
filled box. It follows the tested byte layout but was not run on an ESP32.

```cpp
#include <BluetoothSerial.h>

BluetoothSerial bt;
const int W = 240, H = 160, NB = W / 8;       // 30 x 20 mm

void send(const uint8_t *p, size_t n) { bt.write(p, n); }

void setup() {
  Serial.begin(115200);
  bt.begin("esp32-e210", true);               // true = master
  if (!bt.connect("E210")) {                  // or bt.connect(address)
    Serial.println("connect failed");
    return;
  }
  const uint8_t head[] = {
    0x1C, 0x21, 0x06,                         // density 6
    0x1C, 0x25, 0x02,                         // gap labels
    0x1B, 0x33,                               // start of job
    0x1B, 0x29, (H + 1) & 0xFF, (H + 1) >> 8, // rows + 1
    300 & 0xFF, 300 >> 8,                     // width 30.0 mm
    200 & 0xFF, 200 >> 8,                     // height 20.0 mm
    0x00 };
  send(head, sizeof head);

  uint8_t row[5 + NB];
  for (int y = 0; y < H; y++) {
    for (int b = 0; b < NB; b++) {
      uint8_t v = 0;
      for (int bit = 0; bit < 8; bit++) {
        int x = b * 8 + bit;
        bool frame = x == 0 || x == W - 1 || y == 0 || y == H - 1;
        bool box = x >= 40 && x < 120 && y >= 40 && y < 120;
        if (frame || box) v |= 0x80 >> bit;   // MSB = left, 1 = black
      }
      row[5 + b] = v;
    }
    row[0] = 0x1B; row[1] = 0x19; row[2] = 0x01;
    row[3] = NB + 1;                          // skip byte + data
    row[4] = 0;                               // skip 0 bytes
    send(row, sizeof row);
  }
  const uint8_t tail[] = { 0x1B, 0x4A, 0x00, 0x1B, 0x69 };
  send(tail, sizeof tail);
}

void loop() {
  while (bt.available()) Serial.printf("%02x ", bt.read());
}
```

## BLE

Advertised service UUIDs:

```
0000fee7-0000-1000-8000-00805f9b34fb
ffcacade-afde-cade-defa-cade00000000
00000001-0000-1000-8000-00805f9b34fb
00000003-0000-1000-8000-00805f9b34fb
00000100-0000-1000-8000-00805f9b34fb
```

The boot log on the debug UART ([uart.md](uart.md)) shows the GATT setup:
four services, each with a write ("rx"), a notify ("tx") and in most cases a
flow-control characteristic. The 16-bit services named there are `FFF0`
and `FEE7`, plus one tagged `4953`. Handles from the log:

| Service | Characteristic handles (from log)                 |
|---------|---------------------------------------------------|
| 1st     | tx `000e`, rx `0011`, flow control `0013`         |
| `4953`  | write `0017`, tx `0019`, rx `001c`, flow control `001e` |
| `FFF0`  | tx `0022`, rx `0025`                              |
| `FEE7`  | rx `0028`, in `002a`, tx `002d`                   |

Most likely the printer accepts the same byte stream written to an rx/write
characteristic (as many BLE label printers do), with replies as
notifications. **Not verified**: the GATT table could not be read from Linux
because BlueZ 5.64 always connects this dual-mode device over classic
Bluetooth. To explore it, use a phone app such as nRF Connect, or an
ESP32/nRF52 BLE central: connect, enable notifications on the tx
characteristics, write `1B 06` to an rx characteristic and look for the
reply `1B 06 00 04`.
