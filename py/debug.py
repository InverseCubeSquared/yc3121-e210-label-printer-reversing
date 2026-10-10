#!/usr/bin/env python3
"""debug.py - query and control a YC3121 "E210" printer without printing.
Uses rmicronlib.py.

  debug.py info                 status, parameters, model
  debug.py status --watch       print status bytes whenever they change
  debug.py raw 1b 06            send hex bytes, show the reply
  debug.py feed 10              feed 10 mm
  debug.py next                 advance to the next label
  debug.py density 8            set darkness (0-16)
  debug.py paper continuous     set media type
  debug.py reset                ESC A
  debug.py listen               show unsolicited bytes (error codes E1-E8)
  debug.py scan                 look for 2-byte commands that reply
  debug.py log                  follow the debug UART (/dev/ttyUSB0)
"""
import argparse
import sys
import time

import rmicronlib as rm


def hexs(b):
    return b.hex(" ") if b else "(none)"


def cmd_info(p, a):
    print("status : %s" % hexs(p.status()))
    try:
        par = p.params()
        print("params : %s  (dpi %d)" % (par["raw"], par["dpi"]))
    except rm.PrinterError as e:
        print("params : %s" % e)
    print("model  : %s" % hexs(p.model()))


def cmd_status(p, a):
    last = None
    while True:
        s = p.status()
        if s != last:
            print("%s  %s" % (time.strftime("%H:%M:%S"), hexs(s)), flush=True)
            last = s
        if not a.watch:
            return
        time.sleep(a.interval)


def cmd_raw(p, a):
    try:
        data = bytes.fromhex("".join(a.bytes))
    except ValueError:
        sys.exit("raw: give hex bytes, e.g. 1b 06")
    p.t.read(0.05)
    p.write(data)
    print(hexs(p.t.read(a.timeout)))


def cmd_listen(p, a):
    print("listening, ctrl-c to stop", file=sys.stderr)
    while True:
        r = p.t.read(0.5)
        if r:
            note = ", ".join(rm.ERRORS[b] for b in r if b in rm.ERRORS)
            print("%s  %s  %s" % (time.strftime("%H:%M:%S"), hexs(r), note),
                  flush=True)


def cmd_scan(p, a):
    """Send every <prefix> xx, then ESC 06 as a sync; report replies and
    anything that disturbs the sync. Print/feed commands are skipped."""
    skip = {(0x1b, 0x69), (0x1b, 0x4a), (0x1b, 0x29), (0x1b, 0x19),
            (0x1b, 0x06), (0x1b, 0x41)}
    sync = b"\x1b\x06"
    for pre in (int(x, 16) for x in a.prefix):
        for x in range(256):
            if (pre, x) in skip:
                continue
            cmd = bytes([pre, x])
            p.t.read(0.02)
            p.write(cmd)
            r = p.t.read(0.15)
            p.write(sync)
            s = p.t.read(0.25)
            if r or not s.startswith(sync):
                print("%s  reply %s  sync %s" % (cmd.hex(" "), hexs(r),
                                                 hexs(s)), flush=True)
        print("prefix %02x done" % pre, file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dev", help="device (see e210print.py --dev)")
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("info")
    s = sp.add_parser("status")
    s.add_argument("--watch", action="store_true")
    s.add_argument("--interval", type=float, default=0.5)
    s = sp.add_parser("raw")
    s.add_argument("bytes", nargs="+")
    s.add_argument("--timeout", type=float, default=0.5)
    s = sp.add_parser("feed")
    s.add_argument("mm", type=float)
    sp.add_parser("next")
    s = sp.add_parser("density")
    s.add_argument("n", type=int)
    s = sp.add_parser("paper")
    s.add_argument("type", choices=list(rm.PAPER))
    sp.add_parser("reset")
    sp.add_parser("listen")
    s = sp.add_parser("scan")
    s.add_argument("--prefix", nargs="+", default=["1b", "1c", "1d", "10",
                                                   "1f"])
    s = sp.add_parser("log")
    s.add_argument("--port", default="/dev/ttyUSB0")
    s.add_argument("--baud", type=int, default=rm.DebugConsole.BAUD)
    a = ap.parse_args()

    if a.cmd == "log":
        con = rm.DebugConsole(a.port, a.baud)
        try:
            for line in con.lines():
                print("%s  %s" % (time.strftime("%H:%M:%S"), line), flush=True)
        except KeyboardInterrupt:
            pass
        return

    try:
        with rm.E210(a.dev) as p:
            {"info": cmd_info, "status": cmd_status, "raw": cmd_raw,
             "listen": cmd_listen, "scan": cmd_scan,
             "feed": lambda p, a: p.feed(rm.mm(a.mm)),
             "next": lambda p, a: p.next_label(),
             "density": lambda p, a: p.set_density(a.n),
             "paper": lambda p, a: p.set_paper(a.type),
             "reset": lambda p, a: p.reset()}[a.cmd](p, a)
    except rm.PrinterError as e:
        sys.exit("printer: %s" % e)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
