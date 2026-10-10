/* rmicron-bluetooth.h - Bluetooth specifics of the YC3121 "E210" label
 * printer: identifiers for classic SPP and BLE, and an adapter from user
 * supplied stream callbacks to the rm_io interface of rmicron-proto.h.
 * No Bluetooth stack included; bring your own (BlueZ RFCOMM socket, ESP32
 * BluetoothSerial / ESP-IDF SPP, BTstack, NimBLE, ...). See doc/bluetooth.md.
 *
 *     #define RMICRON_IMPLEMENTATION      // in exactly one .c file
 *     #include "rmicron-bluetooth.h"
 *
 *     static int tx(void *ctx, const uint8_t *d, size_t n) { ... }
 *     static int rx(void *ctx, uint8_t *d, size_t n, int ms) { ... }
 *
 *     rm_bt bt;
 *     rm_printer p;
 *     rm_bt_ops ops = { tx, rx, 0, my_ctx, 0, 0 };
 *     rm_bt_init(&p, &bt, &ops);
 *
 * Classic SPP (RFCOMM channel 1, no pairing) is tested: the byte stream is
 * identical to USB. BLE identifiers are taken from the advertisement and the
 * boot log only; printing over BLE is unverified.
 */
#ifndef RMICRON_BLUETOOTH_H
#define RMICRON_BLUETOOTH_H

#include "rmicron-proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Identification */

#define RM_BT_NAME              "E210"
#define RM_BT_NAME_PREFIX       "E2"    // the vendor app's match for this family
#define RM_BT_CLASS_OF_DEVICE   0x040680UL      // imaging / printer

/* Classic: Serial Port Profile */

#define RM_BT_SPP_UUID          "00001101-0000-1000-8000-00805f9b34fb"
#define RM_BT_SPP_UUID16        0x1101
#define RM_BT_RFCOMM_CHANNEL    1

/* BLE: advertised service UUIDs (unverified for printing) */

#define RM_BLE_UUID_FEE7        "0000fee7-0000-1000-8000-00805f9b34fb"
#define RM_BLE_UUID_VENDOR      "ffcacade-afde-cade-defa-cade00000000"
#define RM_BLE_UUID_FFF0        "0000fff0-0000-1000-8000-00805f9b34fb"  // from boot log
#define RM_BLE_ATT_DEFAULT_MTU  23      // payload per write = MTU - 3

typedef struct {
    /* Send len bytes. Return bytes sent (may be fewer), < 0 on error. */
    int (*send)(void *ctx, const uint8_t *data, size_t len);
    /* Receive up to cap bytes within timeout_ms. Return bytes, 0 on
     * timeout, < 0 on error. May be NULL. */
    int (*recv)(void *ctx, uint8_t *buf, size_t cap, int timeout_ms);
    /* Optional: wait ms milliseconds, used for pacing (may be NULL). */
    void (*delay_ms)(void *ctx, int ms);
    void *ctx;
    size_t chunk;                       // max bytes per send, 0 = unlimited (SPP)
    int pace_ms;                        // pause after each chunk, needs delay_ms
} rm_bt_ops;

typedef struct {
    rm_bt_ops ops;
} rm_bt;

/* Wires p to the Bluetooth callbacks; bt must outlive p. */
void rm_bt_init(rm_printer *p, rm_bt *bt, const rm_bt_ops *ops);

/* 1 if a discovered device name looks like an E210 family printer. */
int rm_bt_name_match(const char *name);

/* Parses "AA:BB:CC:DD:EE:FF" into addr[0..5] in the printed order
 * (addr[0] = 0xAA). BlueZ bdaddr_t wants the reverse order. Returns
 * RM_OK or RM_ERR_ARG. */
int rm_bt_parse_addr(const char *s, uint8_t addr[6]);

#ifdef __cplusplus
}
#endif
#endif /* RMICRON_BLUETOOTH_H */


/* Implementation */

#if defined(RMICRON_IMPLEMENTATION) && !defined(RMICRON_BLUETOOTH_IMPLEMENTED)
#define RMICRON_BLUETOOTH_IMPLEMENTED

static int rm__bt_write(void *ctx, const uint8_t *data, size_t len)
{
    rm_bt *b = (rm_bt *)ctx;
    size_t pos = 0;

    while (pos < len) {
        size_t n = len - pos;
        int rc;
        if (b->ops.chunk && n > b->ops.chunk)
            n = b->ops.chunk;
        rc = b->ops.send(b->ops.ctx, data + pos, n);
        if (rc <= 0)
            return -1;
        pos += (size_t)rc;
        if (b->ops.pace_ms && b->ops.delay_ms)
            b->ops.delay_ms(b->ops.ctx, b->ops.pace_ms);
    }
    return (int)len;
}

static int rm__bt_read(void *ctx, uint8_t *buf, size_t cap, int timeout_ms)
{
    rm_bt *b = (rm_bt *)ctx;
    return b->ops.recv(b->ops.ctx, buf, cap, timeout_ms);
}

void rm_bt_init(rm_printer *p, rm_bt *bt, const rm_bt_ops *ops)
{
    rm_io io;

    bt->ops = *ops;
    io.write = rm__bt_write;
    io.read = ops->recv ? rm__bt_read : 0;
    io.ctx = bt;
    rm_init(p, &io);
}

int rm_bt_name_match(const char *name)
{
    return name && name[0] == RM_BT_NAME_PREFIX[0] &&
           name[1] == RM_BT_NAME_PREFIX[1];
}

static int rm__hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int rm_bt_parse_addr(const char *s, uint8_t addr[6])
{
    int i;

    for (i = 0; i < 6; i++) {
        int hi = rm__hexval(s[0]), lo = hi < 0 ? -1 : rm__hexval(s[1]);
        if (lo < 0 || (i < 5 ? s[2] != ':' : s[2] != '\0'))
            return RM_ERR_ARG;
        addr[i] = (uint8_t)(hi << 4 | lo);
        s += 3;
    }
    return RM_OK;
}

#endif /* RMICRON_IMPLEMENTATION */
