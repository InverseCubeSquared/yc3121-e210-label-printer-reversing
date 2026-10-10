/* rmicron-bt.c - CUPS backend: sends a print job to an E210 over Bluetooth
 * classic SPP (RFCOMM). Uses ../c/rmicron-bluetooth.h; plain Linux sockets,
 * no libbluetooth needed.
 *
 * Device URI: rmicron-bt://AA:BB:CC:DD:EE:FF[?channel=1]
 *
 * Usage (as called by CUPS): rmicron-bt job user title copies options [file]
 * Without arguments it prints the backend line for lpinfo.
 */
#define RMICRON_IMPLEMENTATION
#include "rmicron-bluetooth.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* RFCOMM socket address, as in <bluetooth/rfcomm.h> (avoids libbluetooth-dev) */
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define RM_BTPROTO_RFCOMM 3

struct rm_sockaddr_rc {
    sa_family_t rc_family;
    uint8_t rc_bdaddr[6];               // little endian (reversed) address
    uint8_t rc_channel;
};

/* CUPS backend exit codes (cups/backend.h) */
enum { BE_OK = 0, BE_FAILED = 1, BE_STOP = 4, BE_RETRY = 6 };

static volatile sig_atomic_t canceled;

static void on_term(int sig)
{
    (void)sig;
    canceled = 1;
}

static int sock_send(void *ctx, const uint8_t *d, size_t n)
{
    ssize_t r;
    do
        r = send(*(int *)ctx, d, n, MSG_NOSIGNAL);
    while (r < 0 && errno == EINTR && !canceled);
    return r < 0 ? -1 : (int)r;
}

static int sock_recv(void *ctx, uint8_t *buf, size_t cap, int timeout_ms)
{
    struct pollfd pfd = { *(int *)ctx, POLLIN, 0 };
    ssize_t r;

    if (poll(&pfd, 1, timeout_ms) <= 0)
        return 0;
    r = recv(*(int *)ctx, buf, cap, 0);
    return r < 0 ? -1 : (int)r;
}

static int bt_connect(const uint8_t addr[6], int channel)
{
    struct rm_sockaddr_rc sa;
    int s, i;

    memset(&sa, 0, sizeof sa);
    sa.rc_family = AF_BLUETOOTH;
    for (i = 0; i < 6; i++)
        sa.rc_bdaddr[i] = addr[5 - i];
    sa.rc_channel = (uint8_t)channel;
    s = socket(AF_BLUETOOTH, SOCK_STREAM, RM_BTPROTO_RFCOMM);
    if (s < 0)
        return -1;
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(s);
        return -1;
    }
    return s;
}

/* "rmicron-bt://AA:BB:CC:DD:EE:FF?channel=1" -> addr, channel */
static int parse_uri(const char *uri, uint8_t addr[6], int *channel)
{
    char buf[18];
    const char *p = strstr(uri, "://"), *q;

    if (!p)
        return -1;
    p += 3;
    if (strlen(p) < 17)
        return -1;
    memcpy(buf, p, 17);
    buf[17] = '\0';
    if (rm_bt_parse_addr(buf, addr) != RM_OK)
        return -1;
    *channel = RM_BT_RFCOMM_CHANNEL;
    if ((q = strstr(p, "channel=")) != NULL)
        *channel = atoi(q + 8);
    return *channel >= 1 && *channel <= 30 ? 0 : -1;
}

int main(int argc, char *argv[])
{
    const char *uri = getenv("DEVICE_URI");
    uint8_t addr[6], st[2], buf[4096];
    int channel, fd = 0, sock = -1, tries, rc;
    long total = 0;
    ssize_t n;
    rm_bt bt;
    rm_bt_ops ops;
    rm_printer p;

    if (argc == 1) {
        puts("network rmicron-bt \"Unknown\" \"E210 label printer (Bluetooth)\"");
        return BE_OK;
    }
    if (argc < 6 || argc > 7) {
        fputs("Usage: rmicron-bt job user title copies options [file]\n", stderr);
        return BE_FAILED;
    }
    if (!uri)
        uri = argv[0];
    if (parse_uri(uri, addr, &channel) < 0) {
        fprintf(stderr, "ERROR: bad device URI \"%s\", expected "
                "rmicron-bt://AA:BB:CC:DD:EE:FF\n", uri);
        return BE_STOP;
    }
    if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
        perror("ERROR: unable to open print file");
        return BE_FAILED;
    }
    signal(SIGTERM, on_term);
    signal(SIGPIPE, SIG_IGN);

    fputs("STATE: +connecting-to-device\n", stderr);
    for (tries = 0; tries < 5 && !canceled; tries++) {
        if ((sock = bt_connect(addr, channel)) >= 0)
            break;
        fprintf(stderr, "INFO: Bluetooth connect failed (%s), retrying\n", strerror(errno));
        sleep(3);
    }
    fputs("STATE: -connecting-to-device\n", stderr);
    if (sock < 0) {
        fputs("ERROR: printer not reachable over Bluetooth\n", stderr);
        return BE_RETRY;
    }

    memset(&ops, 0, sizeof ops);
    ops.send = sock_send;
    ops.recv = sock_recv;
    ops.ctx = &sock;
    rm_bt_init(&p, &bt, &ops);

    if (rm_status(&p, st) == RM_OK)
        fprintf(stderr, "DEBUG: printer status %02x %02x\n", st[0], st[1]);
    else
        fputs("WARNING: no status reply, sending anyway\n", stderr);

    rc = RM_OK;
    while (!canceled && rc == RM_OK && (n = read(fd, buf, sizeof buf)) > 0) {
        rc = rm_raw(&p, buf, (size_t)n);
        total += n;
    }
    if (rc != RM_OK) {
        fprintf(stderr, "ERROR: Bluetooth write failed after %ld bytes\n", total);
        close(sock);
        return BE_FAILED;
    }
    fprintf(stderr, "DEBUG: sent %ld bytes\n", total);

    /* give the printer time to report an error byte (E1..E8) */
    if (rm_poll_error(&p, 1500) == RM_ERR_DEVICE) {
        fprintf(stderr, "ERROR: printer reported error 0x%02X\n", p.last_error);
        close(sock);
        return BE_FAILED;
    }
    close(sock);
    if (fd)
        close(fd);
    return BE_OK;
}
