/* test-proto.c - offline tests for the rmicron headers. No hardware needed.
 * Build and run: make test */
#define RMICRON_IMPLEMENTATION
#include "rmicron-usb.h"
#include "rmicron-bluetooth.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); \
    fails++; } } while (0)

/* Capture transport: collects everything written. */
static uint8_t cap_buf[16384];
static size_t cap_len;

static int cap_write(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    if (cap_len + n > sizeof cap_buf)
        return -1;
    memcpy(cap_buf + cap_len, d, n);
    cap_len += n;
    return (int)n;
}

/* Fake reply: echo of the last command plus a canned payload. */
static uint8_t last_cmd[8];
static size_t last_len;
static int fake_bulk_out(void *ctx, uint8_t ep, const uint8_t *d, size_t n, int ms)
{
    (void)ctx; (void)ms;
    CHECK(ep == RM_USB_EP_OUT);
    last_len = n < sizeof last_cmd ? n : sizeof last_cmd;
    memcpy(last_cmd, d, last_len);
    return (int)n;
}

static int pending;
static int fake_intr_in(void *ctx, uint8_t ep, uint8_t *b, size_t cap, int ms)
{
    static const uint8_t params[] = { 0xCB, 0x00, 0x01, 0xE0, 0x01, 0x00, 0x00 };
    size_t n = 0;
    (void)ctx; (void)ms;
    CHECK(ep == RM_USB_EP_IN);
    if (!pending)
        return 0;
    pending = 0;
    memcpy(b, last_cmd, last_len);
    n = last_len;
    if (last_cmd[1] == 0x06) { b[n++] = 0x00; b[n++] = 0x04; }
    if (last_cmd[1] == 0x17) { memcpy(b + n, params, sizeof params); n += sizeof params; }
    (void)cap;
    return (int)n;
}

static int counting_bulk_out(void *ctx, uint8_t ep, const uint8_t *d, size_t n, int ms)
{
    int r = fake_bulk_out(ctx, ep, d, n, ms);
    pending = 1;
    return r;
}

static uint32_t fnv1a(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *d++) * 16777619u;
    return h;
}

static void test_rows(void)
{
    uint8_t b[64];
    static const uint8_t r1[] = { 0x00, 0x40, 0x00 }, e1[] = { 0x1B, 0x19, 0x01, 0x02, 0x01, 0x40 };
    static const uint8_t r2[] = { 0xFF, 0xFF, 0xFF }, e2[] = { 0x1B, 0x19, 0x01, 0x04, 0x00, 0xFF, 0xFF, 0xFF };
    static const uint8_t r3[] = { 0x80, 0x00, 0x01 }, e3[] = { 0x1B, 0x19, 0x01, 0x04, 0x00, 0x80, 0x00, 0x01 };
    static const uint8_t r4[] = { 0x00, 0x00, 0x00 }, e4[] = { 0x1B, 0x19, 0x01, 0x01, 0x03 };

    CHECK(rm_enc_row(b, r1, 3) == sizeof e1 && !memcmp(b, e1, sizeof e1));
    CHECK(rm_enc_row(b, r2, 3) == sizeof e2 && !memcmp(b, e2, sizeof e2));
    CHECK(rm_enc_row(b, r3, 3) == sizeof e3 && !memcmp(b, e3, sizeof e3));
    CHECK(rm_enc_row(b, r4, 3) == sizeof e4 && !memcmp(b, e4, sizeof e4));
}

/* Same image and settings as rmicronlib.encode_job(img, 30, 20) in py/:
 * 240x160, 1-dot frame, filled box 40..119. Golden values from Python. */
static void test_job_matches_python(void)
{
    static uint8_t bits[160][30];
    rm_io io = { cap_write, 0, 0 };
    rm_printer p;
    int x, y;

    memset(bits, 0, sizeof bits);
    for (y = 0; y < 160; y++)
        for (x = 0; x < 240; x++)
            if (x == 0 || x == 239 || y == 0 || y == 159 ||
                (x >= 40 && x < 120 && y >= 40 && y < 120))
                bits[y][x / 8] |= (uint8_t)(0x80 >> (x % 8));
    cap_len = 0;
    rm_init(&p, &io);
    CHECK(rm_print_bitmap(&p, &bits[0][0], 240, 160, 30, 300, 200,
                          RM_PAPER_GAP, 6) == RM_OK);
    CHECK(cap_len == 5630);
    CHECK(fnv1a(cap_buf, cap_len) == 0x25831e47u);
}

static void test_job_state(void)
{
    static const uint8_t row[30];
    rm_io io = { cap_write, 0, 0 };
    rm_printer p;

    rm_init(&p, &io);
    CHECK(rm_job_row(&p, row, 30) == RM_ERR_STATE);
    CHECK(rm_job_begin(&p, 240, 2, 300, 200, -1, -1) == RM_OK);
    CHECK(rm_job_begin(&p, 240, 2, 300, 200, -1, -1) == RM_ERR_STATE);
    CHECK(rm_job_row(&p, row, 29) == RM_ERR_ARG);
    CHECK(rm_job_end(&p) == RM_OK);         // pads the missing rows
    CHECK(rm_job_begin(&p, 999, 2, 300, 200, -1, -1) == RM_ERR_ARG);
    CHECK(rm_status(&p, (uint8_t[2]){ 0 }) == RM_ERR_NOREAD);
}

static void test_usb_queries(void)
{
    rm_usb usb;
    rm_printer p;
    rm_usb_ops ops = { counting_bulk_out, fake_intr_in, 0, 0, 0 };
    uint8_t st[2];
    rm_params par;

    rm_usb_init(&p, &usb, &ops);
    CHECK(rm_status(&p, st) == RM_OK && st[0] == 0x00 && st[1] == 0x04);
    CHECK(rm_get_params(&p, &par) == RM_OK && par.dpi == 203 && par.head_dots == 480);
    CHECK(rm_usb_match(0x08C5, 0x4001) && !rm_usb_match(0x08C5, 0x4002));
}

static void test_bt_helpers(void)
{
    uint8_t a[6];

    CHECK(rm_bt_parse_addr("AA:bb:01:23:45:6F", a) == RM_OK &&
          a[0] == 0xAA && a[1] == 0xBB && a[5] == 0x6F);
    CHECK(rm_bt_parse_addr("AA:BB:CC:DD:EE", a) == RM_ERR_ARG);
    CHECK(rm_bt_parse_addr("AA:BB:CC:DD:EE:FF:00", a) == RM_ERR_ARG);
    CHECK(rm_bt_name_match("E210") && !rm_bt_name_match("P15"));
}

int main(void)
{
    test_rows();
    test_job_matches_python();
    test_job_state();
    test_usb_queries();
    test_bt_helpers();
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
