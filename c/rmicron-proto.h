/* rmicron-proto.h - printer protocol of the YC3121 "E210" label printer
 * (QLabel / com.rmicro.labelprinter command set). C99, no dependencies,
 * no allocation. See doc/protocol.md.
 *
 * Single-header library: in exactly one .c file write
 *
 *     #define RMICRON_IMPLEMENTATION
 *     #include "rmicron-proto.h"
 *
 * The transport is supplied by the caller as two callbacks (write, read),
 * see struct rm_io. rmicron-usb.h and rmicron-bluetooth.h adapt typical
 * USB and Bluetooth stacks to it.
 *
 * Usage:
 *
 *     rm_printer p;
 *     rm_init(&p, &io);
 *     rm_job_begin(&p, 240, 160, 300, 200, RM_PAPER_GAP, 6);
 *     for (y = 0; y < 160; y++)
 *         rm_job_row(&p, row[y], 30);      // 1 bit per dot, MSB left, 1 = black
 *     rm_job_end(&p);
 */
#ifndef RMICRON_PROTO_H
#define RMICRON_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */

#define RM_DOTS_PER_MM      8           // 203 dpi
#define RM_DPI              203
#define RM_HEAD_DOTS        384         // 48 mm, confirmed printable width
#define RM_HEAD_DOTS_MAX    480         // reported by ESC 0x17, unverified
#define RM_ROW_BYTES_MAX    (RM_HEAD_DOTS_MAX / 8)
#define RM_DENSITY_DEFAULT  6
#define RM_DENSITY_MAX      16

typedef enum {
    RM_PAPER_CONTINUOUS = 0,
    RM_PAPER_HOLES      = 1,
    RM_PAPER_GAP        = 2,
    RM_PAPER_BLACKMARK  = 3,
    RM_PAPER_CABLE      = 4
} rm_paper;

/* Return codes; negative values are errors. */
enum {
    RM_OK         = 0,
    RM_ERR_IO     = -1,             // write callback failed
    RM_ERR_ARG    = -2,             // invalid argument
    RM_ERR_STATE  = -3,             // job call out of order
    RM_ERR_REPLY  = -4,             // no or malformed reply
    RM_ERR_NOREAD = -5,             // transport has no read callback
    RM_ERR_DEVICE = -6              // printer sent an error byte (E1..E8)
};

/* Transport callbacks */

typedef struct {
    /* Write len bytes. Return the number written (len) or < 0 on error. */
    int (*write)(void *ctx, const uint8_t *data, size_t len);
    /* Read up to cap bytes, waiting at most timeout_ms (always >= 1; many
     * stacks treat 0 as "forever") for the first byte.
     * Return bytes read, 0 on timeout, < 0 on error. May be NULL for
     * write-only transports (queries then return RM_ERR_NOREAD). */
    int (*read)(void *ctx, uint8_t *buf, size_t cap, int timeout_ms);
    void *ctx;
} rm_io;

typedef struct {
    rm_io io;
    int in_job;                         // between rm_job_begin and rm_job_end
    int rows_left;
    int row_bytes;
    int offset_h, offset_v;             // sent with every job (rm_set_offset)
    uint8_t last_error;                 // last E1..E8 byte seen, 0 = none
} rm_printer;

typedef struct {
    uint16_t dpi;                       // 203
    uint16_t head_dots;                 // 480 on the tested unit (meaning unverified)
    uint8_t raw[16];                    // reply payload after the echo
    size_t raw_len;
} rm_params;

/* Setup */

void rm_init(rm_printer *p, const rm_io *io);

/* Command encoders: write the command into buf (at least the documented
 * size) and return its length. No I/O. */

size_t rm_enc_density(uint8_t *buf, int density);               // 3 bytes
size_t rm_enc_offset(uint8_t *buf, int h, int v);               // 8 bytes
size_t rm_enc_paper(uint8_t *buf, rm_paper paper);              // 3 bytes
size_t rm_enc_start(uint8_t *buf);                              // 2 bytes
size_t rm_enc_label(uint8_t *buf, int rows, int width_01mm,
                    int height_01mm);                           // 9 bytes
size_t rm_enc_row(uint8_t *buf, const uint8_t *row,
                  int row_bytes);                               // 5 + row_bytes
size_t rm_enc_end(uint8_t *buf);                                // 5 bytes
size_t rm_enc_feed(uint8_t *buf, int dots);                     // 3 bytes
size_t rm_enc_next(uint8_t *buf);                               // 2 bytes

/* Settings and motion (sent immediately) */

int rm_set_density(rm_printer *p, int density);                 // 0..16
int rm_set_offset(rm_printer *p, int h, int v);                 // printer units, signed; kept for later jobs
int rm_set_paper(rm_printer *p, rm_paper paper);
int rm_feed(rm_printer *p, int dots);                           // any length, 8 dots/mm
int rm_next_label(rm_printer *p);
int rm_reset(rm_printer *p);
int rm_raw(rm_printer *p, const uint8_t *data, size_t len);

/* Printing, streamed row by row.
 * width_dots: image width (<= RM_HEAD_DOTS_MAX), left aligned on the label.
 * rows: image height in dots. Label size in 0.1 mm; for continuous paper
 * pass height_01mm = rows * 10 / 8. paper < 0 or density < 0 skip that
 * setting (the printer keeps its current value). */
int rm_job_begin(rm_printer *p, int width_dots, int rows, int width_01mm,
                 int height_01mm, int paper, int density);
int rm_job_row(rm_printer *p, const uint8_t *row, int row_bytes);
int rm_job_end(rm_printer *p);

/* Whole 1 bpp bitmap: stride bytes per row, MSB left, 1 = black. */
int rm_print_bitmap(rm_printer *p, const uint8_t *bits, int width_dots,
                    int rows, int stride, int width_01mm, int height_01mm,
                    int paper, int density);

/* Queries (need a read callback) */

int rm_query(rm_printer *p, const uint8_t *cmd, size_t cmd_len,
             uint8_t *reply, size_t cap, int timeout_ms);       // payload after echo
int rm_status(rm_printer *p, uint8_t status[2]);                // 00 04 = idle
int rm_get_params(rm_printer *p, rm_params *out);

/* Reads pending bytes and returns RM_ERR_DEVICE if an error byte
 * (E1..E8) arrived, RM_OK otherwise. p->last_error holds the byte. */
int rm_poll_error(rm_printer *p, int timeout_ms);

const char *rm_strerror(int code);

#ifdef __cplusplus
}
#endif
#endif /* RMICRON_PROTO_H */


/* Implementation */

#if defined(RMICRON_IMPLEMENTATION) && !defined(RMICRON_PROTO_IMPLEMENTED)
#define RMICRON_PROTO_IMPLEMENTED

#include <string.h>

static int rm__clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int rm__send(rm_printer *p, const uint8_t *data, size_t len)
{
    if (!p->io.write)
        return RM_ERR_IO;
    return p->io.write(p->io.ctx, data, len) == (int)len ? RM_OK : RM_ERR_IO;
}

void rm_init(rm_printer *p, const rm_io *io)
{
    memset(p, 0, sizeof *p);
    p->io = *io;
}

size_t rm_enc_density(uint8_t *b, int density)
{
    b[0] = 0x1C; b[1] = 0x21; b[2] = (uint8_t)rm__clamp(density, 0, RM_DENSITY_MAX);
    return 3;
}

size_t rm_enc_offset(uint8_t *b, int h, int v)
{
    b[0] = 0x1B; b[1] = 0x35; b[2] = h < 0; b[3] = (uint8_t)rm__clamp(h < 0 ? -h : h, 0, 255);
    b[4] = 0x1B; b[5] = 0x34; b[6] = v < 0; b[7] = (uint8_t)rm__clamp(v < 0 ? -v : v, 0, 255);
    return 8;
}

size_t rm_enc_paper(uint8_t *b, rm_paper paper)
{
    b[0] = 0x1C; b[1] = 0x25; b[2] = (uint8_t)paper;
    return 3;
}

size_t rm_enc_start(uint8_t *b)
{
    b[0] = 0x1B; b[1] = 0x33;
    return 2;
}

size_t rm_enc_label(uint8_t *b, int rows, int width_01mm, int height_01mm)
{
    unsigned r = (unsigned)rows + 1, w = (unsigned)width_01mm, h = (unsigned)height_01mm;
    b[0] = 0x1B; b[1] = 0x29;
    b[2] = r & 0xFF; b[3] = (r >> 8) & 0xFF;
    b[4] = w & 0xFF; b[5] = (w >> 8) & 0xFF;
    b[6] = h & 0xFF; b[7] = (h >> 8) & 0xFF;
    b[8] = 0;
    return 9;
}

size_t rm_enc_row(uint8_t *b, const uint8_t *row, int n)
{
    int front = 0, back = 0, len;

    while (front < n && row[front] == 0)
        front++;
    if (front < n)
        while (row[n - 1 - back] == 0)
            back++;
    len = n - front - back;
    b[0] = 0x1B; b[1] = 0x19; b[2] = 0x01;
    b[3] = (uint8_t)(len + 1);              // skip byte + data
    b[4] = (uint8_t)front;
    memcpy(b + 5, row + front, (size_t)len);
    return 5 + (size_t)len;
}

size_t rm_enc_end(uint8_t *b)
{
    b[0] = 0x1B; b[1] = 0x4A; b[2] = 0x00;  // feed 0
    b[3] = 0x1B; b[4] = 0x69;               // print, next label
    return 5;
}

size_t rm_enc_feed(uint8_t *b, int dots)
{
    b[0] = 0x1B; b[1] = 0x4A; b[2] = (uint8_t)rm__clamp(dots, 0, 255);
    return 3;
}

size_t rm_enc_next(uint8_t *b)
{
    b[0] = 0x1B; b[1] = 0x69;
    return 2;
}

int rm_set_density(rm_printer *p, int density)
{
    uint8_t b[3];
    return rm__send(p, b, rm_enc_density(b, density));
}

int rm_set_offset(rm_printer *p, int h, int v)
{
    uint8_t b[8];
    p->offset_h = h;
    p->offset_v = v;
    return rm__send(p, b, rm_enc_offset(b, h, v));
}

int rm_set_paper(rm_printer *p, rm_paper paper)
{
    uint8_t b[3];
    if (paper < RM_PAPER_CONTINUOUS || paper > RM_PAPER_CABLE)
        return RM_ERR_ARG;
    return rm__send(p, b, rm_enc_paper(b, paper));
}

int rm_feed(rm_printer *p, int dots)
{
    uint8_t b[3];
    int rc = RM_OK;

    while (dots > 0 && rc == RM_OK) {
        rc = rm__send(p, b, rm_enc_feed(b, dots > 255 ? 255 : dots));
        dots -= 255;
    }
    return rc;
}

int rm_next_label(rm_printer *p)
{
    uint8_t b[2];
    return rm__send(p, b, rm_enc_next(b));
}

int rm_reset(rm_printer *p)
{
    static const uint8_t b[2] = { 0x1B, 0x41 };
    return rm__send(p, b, sizeof b);
}

int rm_raw(rm_printer *p, const uint8_t *data, size_t len)
{
    return rm__send(p, data, len);
}

int rm_job_begin(rm_printer *p, int width_dots, int rows, int width_01mm,
                 int height_01mm, int paper, int density)
{
    uint8_t b[32];
    size_t n = 0;

    if (p->in_job)
        return RM_ERR_STATE;
    if (width_dots < 1 || width_dots > RM_HEAD_DOTS_MAX || rows < 1 ||
        rows > 65534 || width_01mm < 1 || width_01mm > 65535 ||
        height_01mm < 1 || height_01mm > 65535 || paper > RM_PAPER_CABLE)
        return RM_ERR_ARG;
    if (density >= 0)
        n += rm_enc_density(b + n, density);
    n += rm_enc_offset(b + n, p->offset_h, p->offset_v);
    if (paper >= 0)
        n += rm_enc_paper(b + n, (rm_paper)paper);
    n += rm_enc_start(b + n);
    n += rm_enc_label(b + n, rows, width_01mm, height_01mm);
    if (rm__send(p, b, n) != RM_OK)
        return RM_ERR_IO;
    p->in_job = 1;
    p->rows_left = rows;
    p->row_bytes = (width_dots + 7) / 8;
    return RM_OK;
}

int rm_job_row(rm_printer *p, const uint8_t *row, int row_bytes)
{
    uint8_t b[5 + RM_ROW_BYTES_MAX];

    if (!p->in_job || p->rows_left <= 0)
        return RM_ERR_STATE;
    if (row_bytes != p->row_bytes)
        return RM_ERR_ARG;
    p->rows_left--;
    return rm__send(p, b, rm_enc_row(b, row, row_bytes));
}

int rm_job_end(rm_printer *p)
{
    static const uint8_t blank[RM_ROW_BYTES_MAX];
    uint8_t b[5];
    int rc = RM_OK;

    if (!p->in_job)
        return RM_ERR_STATE;
    while (p->rows_left > 0 && rc == RM_OK)     // pad missing rows with white
        rc = rm_job_row(p, blank, p->row_bytes);
    p->in_job = 0;
    if (rc != RM_OK)
        return rc;
    return rm__send(p, b, rm_enc_end(b));
}

int rm_print_bitmap(rm_printer *p, const uint8_t *bits, int width_dots,
                    int rows, int stride, int width_01mm, int height_01mm,
                    int paper, int density)
{
    int y, rc, nb = (width_dots + 7) / 8;

    if (stride < nb)
        return RM_ERR_ARG;
    rc = rm_job_begin(p, width_dots, rows, width_01mm, height_01mm, paper, density);
    for (y = 0; y < rows && rc == RM_OK; y++)
        rc = rm_job_row(p, bits + (size_t)y * (size_t)stride, nb);
    if (rc != RM_OK) {
        p->in_job = 0;
        return rc;
    }
    return rm_job_end(p);
}

/* Drain bytes that are already waiting, remembering error bytes. */
static void rm__drain(rm_printer *p, int timeout_ms)
{
    uint8_t b[64];
    int n, i;

    while ((n = p->io.read(p->io.ctx, b, sizeof b, timeout_ms)) > 0)
        for (i = 0; i < n; i++)
            if (b[i] >= 0xE1 && b[i] <= 0xE8)
                p->last_error = b[i];
}

int rm_query(rm_printer *p, const uint8_t *cmd, size_t cmd_len,
             uint8_t *reply, size_t cap, int timeout_ms)
{
    uint8_t b[64];
    size_t got = 0, i;
    int n;

    if (!p->io.read)
        return RM_ERR_NOREAD;
    rm__drain(p, 1);
    if (rm__send(p, cmd, cmd_len) != RM_OK)
        return RM_ERR_IO;
    while (got < sizeof b &&
           (n = p->io.read(p->io.ctx, b + got, sizeof b - got,
                           got ? 50 : timeout_ms)) > 0)
        got += (size_t)n;
    if (got < cmd_len || memcmp(b, cmd, cmd_len) != 0)
        return RM_ERR_REPLY;
    got -= cmd_len;
    for (i = 0; i < got && i < cap; i++)
        reply[i] = b[cmd_len + i];
    return (int)i;
}

int rm_status(rm_printer *p, uint8_t status[2])
{
    static const uint8_t cmd[2] = { 0x1B, 0x06 };
    int n = rm_query(p, cmd, sizeof cmd, status, 2, 500);
    return n < 0 ? n : n == 2 ? RM_OK : RM_ERR_REPLY;
}

int rm_get_params(rm_printer *p, rm_params *out)
{
    static const uint8_t cmd[2] = { 0x1B, 0x17 };
    int n = rm_query(p, cmd, sizeof cmd, out->raw, sizeof out->raw, 500);

    if (n < 0)
        return n;
    if (n < 5)
        return RM_ERR_REPLY;
    out->raw_len = (size_t)n;
    out->dpi = (uint16_t)(out->raw[0] | out->raw[1] << 8);
    out->head_dots = (uint16_t)(out->raw[3] | out->raw[4] << 8);
    return RM_OK;
}

int rm_poll_error(rm_printer *p, int timeout_ms)
{
    if (!p->io.read)
        return RM_ERR_NOREAD;
    p->last_error = 0;
    rm__drain(p, timeout_ms < 1 ? 1 : timeout_ms);
    return p->last_error ? RM_ERR_DEVICE : RM_OK;
}

const char *rm_strerror(int code)
{
    switch (code) {
    case RM_OK:         return "ok";
    case RM_ERR_IO:     return "transport write failed";
    case RM_ERR_ARG:    return "invalid argument";
    case RM_ERR_STATE:  return "job call out of order";
    case RM_ERR_REPLY:  return "no or malformed reply";
    case RM_ERR_NOREAD: return "transport cannot read";
    case RM_ERR_DEVICE: return "printer reported an error";
    default:            return "unknown error";
    }
}

#endif /* RMICRON_IMPLEMENTATION */
