/* rastertormicron.c - CUPS filter: CUPS raster (1 bit, K) -> E210 printer
 * commands on stdout. Uses ../c/rmicron-proto.h.
 *
 * Usage (as called by CUPS): rastertormicron job user title copies options [file]
 *
 * Page header fields used:
 *   PageSize        label size in points -> 0.1 mm
 *   cupsMediaType   paper type (0 continuous, 1 holes, 2 gap, 3 black mark)
 *   cupsCompression density 1..16 (PPD option "Darkness"), 0 = printer default
 * On continuous paper white rows at the top and bottom are dropped (2 mm
 * kept), so receipts are only as long as their content.
 */
#define RMICRON_IMPLEMENTATION
#include "rmicron-proto.h"

#include <cups/cups.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t canceled;

static void on_term(int sig)
{
    (void)sig;
    canceled = 1;
}

static int out_write(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    return fwrite(d, 1, n, stdout) == n ? (int)n : -1;
}

static int row_blank(const unsigned char *r, unsigned n)
{
    while (n--)
        if (*r++)
            return 0;
    return 1;
}

/* One page: read all lines, crop to the head width, send as one job. */
static int print_page(rm_printer *p, cups_raster_t *ras, cups_page_header2_t *h)
{
    unsigned bpl = h->cupsBytesPerLine, rows = h->cupsHeight, first, y;
    unsigned nb = bpl < RM_HEAD_DOTS / 8 ? bpl : RM_HEAD_DOTS / 8;
    unsigned width_dots = h->cupsWidth < RM_HEAD_DOTS ? h->cupsWidth : RM_HEAD_DOTS;
    int w01 = (int)(h->cupsPageSize[0] * 254.0f / 72.0f + 0.5f);
    int h01 = (int)(h->cupsPageSize[1] * 254.0f / 72.0f + 0.5f);
    int paper = h->cupsMediaType <= RM_PAPER_CABLE ? (int)h->cupsMediaType : RM_PAPER_GAP;
    int density = h->cupsCompression >= 1 && h->cupsCompression <= RM_DENSITY_MAX
                  ? (int)h->cupsCompression : -1;
    unsigned char *page;
    int rc;

    if (h->cupsBitsPerPixel != 1) {
        fprintf(stderr, "ERROR: expected 1 bit per pixel, got %u\n", h->cupsBitsPerPixel);
        return -1;
    }
    if (rows == 0 || rows > 65534)
        return -1;
    page = malloc((size_t)bpl * rows);
    if (!page) {
        fputs("ERROR: out of memory\n", stderr);
        return -1;
    }
    for (y = 0; y < rows; y++)
        if (cupsRasterReadPixels(ras, page + (size_t)y * bpl, bpl) != bpl)
            break;
    rows = y;

    first = 0;
    if (paper == RM_PAPER_CONTINUOUS) {         // drop white rows at both ends
        unsigned margin = 2 * RM_DOTS_PER_MM;   // keep 2 mm
        while (rows > 0 && row_blank(page + (size_t)(rows - 1) * bpl, nb))
            rows--;
        if (rows == 0) {                        // blank page
            free(page);
            return 0;
        }
        while (first < rows && row_blank(page + (size_t)first * bpl, nb))
            first++;
        first = first > margin ? first - margin : 0;
        rows = rows + margin < h->cupsHeight ? rows + margin : h->cupsHeight;
        rows -= first;
        h01 = (int)(rows * 10 / RM_DOTS_PER_MM);
    }
    if (rows == 0 || h01 < 1 || w01 < 1) {
        free(page);
        return 0;
    }

    rc = rm_job_begin(p, (int)width_dots, (int)rows, w01, h01, paper, density);
    for (y = first; y < first + rows && rc == RM_OK && !canceled; y++)
        rc = rm_job_row(p, page + (size_t)y * bpl, (int)nb);
    if (rc == RM_OK)
        rc = rm_job_end(p);
    free(page);
    if (rc != RM_OK) {
        fprintf(stderr, "ERROR: %s\n", rm_strerror(rc));
        return -1;
    }
    fprintf(stderr, "DEBUG: page %ux%u dots, label %.1fx%.1f mm, paper %d, density %d\n",
            width_dots, rows, w01 / 10.0, h01 / 10.0, paper, density);
    return 0;
}

int main(int argc, char *argv[])
{
    int fd = 0, pages = 0;
    cups_raster_t *ras;
    cups_page_header2_t header;
    rm_io io = { out_write, 0, 0 };
    rm_printer p;

    if (argc < 6 || argc > 7) {
        fputs("Usage: rastertormicron job user title copies options [file]\n", stderr);
        return 1;
    }
    if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
        perror("ERROR: unable to open raster file");
        return 1;
    }
    signal(SIGTERM, on_term);

    rm_init(&p, &io);
    ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
    while (!canceled && cupsRasterReadHeader2(ras, &header)) {
        pages++;
        fprintf(stderr, "PAGE: %d %u\n", pages, header.NumCopies);
        if (print_page(&p, ras, &header) < 0)
            break;
        fflush(stdout);
    }
    cupsRasterClose(ras);
    if (fd)
        close(fd);
    if (pages == 0) {
        fputs("ERROR: no pages found\n", stderr);
        return 1;
    }
    fputs("INFO: ready to print\n", stderr);
    return 0;
}
