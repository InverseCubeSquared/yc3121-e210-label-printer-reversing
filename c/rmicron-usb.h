/* rmicron-usb.h - USB specifics of the YC3121 "E210" label printer:
 * descriptors, endpoints and an adapter from user supplied USB transfer
 * callbacks to the rm_io interface of rmicron-proto.h. No USB stack
 * included; bring your own (libusb, TinyUSB host, ESP-IDF, ...).
 * See doc/usb.md.
 *
 *     #define RMICRON_IMPLEMENTATION      // in exactly one .c file
 *     #include "rmicron-usb.h"
 *
 *     static int out(void *ctx, uint8_t ep, const uint8_t *d, size_t n, int ms)
 *     { ... bulk transfer on ep, return bytes written or < 0 ... }
 *     static int in(void *ctx, uint8_t ep, uint8_t *d, size_t n, int ms)
 *     { ... interrupt transfer on ep, return bytes, 0 on timeout ... }
 *
 *     rm_usb usb;
 *     rm_printer p;
 *     rm_usb_ops ops = { out, in, my_ctx, 0, 0 };
 *     rm_usb_init(&p, &usb, &ops);
 *     rm_status(&p, st);
 */
#ifndef RMICRON_USB_H
#define RMICRON_USB_H

#include "rmicron-proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Descriptors */

#define RM_USB_VID              0x08C5
#define RM_USB_PID              0x4001
#define RM_USB_MANUFACTURER     "YICHIP"
#define RM_USB_PRODUCT          "YC3121 Printer demo"   // SDK default, not unique
#define RM_USB_INTERFACE        0
#define RM_USB_CLASS            0x07    // printer
#define RM_USB_SUBCLASS         0x01
#define RM_USB_PROTOCOL         0x02    // bidirectional
#define RM_USB_EP_OUT           0x01    // bulk OUT: commands, print data
#define RM_USB_EP_IN            0x81    // interrupt IN: replies
#define RM_USB_MAX_PACKET       64
#define RM_USB_DEVICE_ID        "printer;"              // IEEE 1284, no MFG/CMD

/* Printer class control requests (bmRequestType 0xA1). GET_DEVICE_ID
 * answers RM_USB_DEVICE_ID; GET_PORT_STATUS is not answered. */
#define RM_USB_REQ_GET_DEVICE_ID    0x00
#define RM_USB_REQ_GET_PORT_STATUS  0x01

#define RM_USB_CHUNK_DEFAULT    4096
#define RM_USB_TIMEOUT_DEFAULT  10000   // ms per OUT transfer

typedef struct {
    /* Bulk OUT transfer on endpoint ep. Return bytes written, < 0 on error. */
    int (*bulk_out)(void *ctx, uint8_t ep, const uint8_t *data, size_t len,
                    int timeout_ms);
    /* Interrupt IN transfer on endpoint ep (one packet, <= 64 bytes).
     * Return bytes read, 0 on timeout, < 0 on error. May be NULL. */
    int (*intr_in)(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
                   int timeout_ms);
    void *ctx;
    size_t chunk;                       // max bytes per OUT transfer, 0 = default
    int timeout_ms;                     // per OUT transfer, 0 = default
} rm_usb_ops;

typedef struct {
    rm_usb_ops ops;
} rm_usb;

/* Wires p to the USB callbacks; usb must outlive p. */
void rm_usb_init(rm_printer *p, rm_usb *usb, const rm_usb_ops *ops);

/* 1 if the ids belong to an E210. */
int rm_usb_match(uint16_t vid, uint16_t pid);

#ifdef __cplusplus
}
#endif
#endif /* RMICRON_USB_H */


/* Implementation */

#if defined(RMICRON_IMPLEMENTATION) && !defined(RMICRON_USB_IMPLEMENTED)
#define RMICRON_USB_IMPLEMENTED

static int rm__usb_write(void *ctx, const uint8_t *data, size_t len)
{
    rm_usb *u = (rm_usb *)ctx;
    size_t chunk = u->ops.chunk ? u->ops.chunk : RM_USB_CHUNK_DEFAULT;
    int tmo = u->ops.timeout_ms ? u->ops.timeout_ms : RM_USB_TIMEOUT_DEFAULT;
    size_t pos = 0;

    while (pos < len) {
        size_t n = len - pos < chunk ? len - pos : chunk;
        int rc = u->ops.bulk_out(u->ops.ctx, RM_USB_EP_OUT, data + pos, n, tmo);
        if (rc <= 0)
            return -1;
        pos += (size_t)rc;
    }
    return (int)len;
}

static int rm__usb_read(void *ctx, uint8_t *buf, size_t cap, int timeout_ms)
{
    rm_usb *u = (rm_usb *)ctx;

    if (cap > RM_USB_MAX_PACKET)        // one interrupt packet per call
        cap = RM_USB_MAX_PACKET;
    return u->ops.intr_in(u->ops.ctx, RM_USB_EP_IN, buf, cap, timeout_ms);
}

void rm_usb_init(rm_printer *p, rm_usb *usb, const rm_usb_ops *ops)
{
    rm_io io;

    usb->ops = *ops;
    io.write = rm__usb_write;
    io.read = ops->intr_in ? rm__usb_read : 0;
    io.ctx = usb;
    rm_init(p, &io);
}

int rm_usb_match(uint16_t vid, uint16_t pid)
{
    return vid == RM_USB_VID && pid == RM_USB_PID;
}

#endif /* RMICRON_IMPLEMENTATION */
