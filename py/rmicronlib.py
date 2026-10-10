"""rmicronlib - driver for the YC3121 "E210" thermal label printer
(QLabel / com.rmicro.labelprinter protocol). See PROTOCOL.md.

    from rmicronlib import E210, text_image

    with E210() as p:                        # first E210 on USB
        img = text_image("Hello", *p.size_dots(40, 30))
        p.print_image(img, 40, 30)
        print(p.status())

Transports: USB (pyusb, or /dev/usb/lp* write-only), Bluetooth SPP
(RFCOMM socket, Linux) and any byte stream object with write()/read().
Rendering helpers turn text, Code 128, QR codes and image files into
Pillow "L" images (0 = black) sized in printer dots.
"""
import glob
import socket
import struct
import subprocess
import sys
import time

from PIL import Image, ImageDraw, ImageFont, ImageOps

VID, PID = 0x08C5, 0x4001
DOTS_PER_MM = 8                    # 203 dpi
HEAD_DOTS = 384                    # 48 mm printable width

PAPER = {"continuous": 0, "holes": 1, "gap": 2, "blackmark": 3, "cable": 4}

# name -> (width mm, height mm, paper); height None = continuous, sized
# to the content. Widths above HEAD_DOTS are printed on the left 48 mm.
PRESETS = {
    "15x30": (15, 30, "gap"), "20x10": (20, 10, "gap"),
    "25x15": (25, 15, "gap"), "30x15": (30, 15, "gap"),
    "30x20": (30, 20, "gap"), "30x40": (30, 40, "gap"),
    "40x20": (40, 20, "gap"), "40x30": (40, 30, "gap"),
    "40x40": (40, 40, "gap"), "40x60": (40, 60, "gap"),
    "48x30": (48, 30, "gap"), "50x20": (50, 20, "gap"),
    "50x25": (50, 25, "gap"), "50x30": (50, 30, "gap"),
    "50x40": (50, 40, "gap"), "50x50": (50, 50, "gap"),
    "50x80": (50, 80, "gap"),
    "receipt57": (48, None, "continuous"),   # 57/58 mm till roll
    "receipt48": (48, None, "continuous"),
    "receipt40": (40, None, "continuous"),
}

FONTS = ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"]
MONO = ["/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"]


class PrinterError(Exception):
    pass


# ---------- rendering: everything is an "L" image, 0 = black ---------------

def mm(v):
    return round(v * DOTS_PER_MM)


def load_font(size, paths=None):
    for p in ([paths] if isinstance(paths, str) else paths or FONTS):
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def text_image(text, w, h, font=None):
    """Text centred on a w x h label at the largest size that fits."""
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    best, box = load_font(10, font), None
    for size in range(h, 5, -1):
        f = load_font(size, font)
        b = d.multiline_textbbox((0, 0), text, font=f, align="center")
        if b[2] - b[0] <= w - 8 and b[3] - b[1] <= h - 8:
            best, box = f, b
            break
    if box is None:
        box = d.multiline_textbbox((0, 0), text, font=best, align="center")
    x = (w - (box[2] - box[0])) // 2 - box[0]
    y = (h - (box[3] - box[1])) // 2 - box[1]
    d.multiline_text((x, y), text, font=best, fill=0, align="center")
    return img


def receipt_image(text, w, size=22, font=None, margin=8):
    """Left-aligned, word-wrapped text of fixed size; height follows the
    content. For continuous (receipt) paper."""
    f = load_font(size, font or MONO)
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    lines = []
    for para in text.split("\n"):
        line = None                                 # keeps leading spaces
        for word in para.split(" "):
            t = word if line is None else line + " " + word
            if line is None or probe.textlength(t, font=f) <= w - 2 * margin:
                line = t
            else:
                lines.append(line)
                line = word
        lines.append(line)
    asc, desc = f.getmetrics()
    step = asc + desc + 2
    img = Image.new("L", (w, step * len(lines) + 2 * margin), 255)
    d = ImageDraw.Draw(img)
    for i, line in enumerate(lines):
        d.text((margin, margin + i * step), line, font=f, fill=0)
    return img


def file_image(path, w, h, rotate=0):
    """Image file scaled to fit w x h (keeping aspect), centred.
    h None = keep aspect, height follows (continuous paper)."""
    src = Image.open(path).convert("RGBA")
    flat = Image.new("RGBA", src.size, "white")
    flat.alpha_composite(src)
    img = flat.convert("L")
    if rotate:
        img = img.rotate(-rotate, expand=True, fillcolor=255)
    if h is None:
        if img.width != w:
            img = img.resize((w, max(1, round(img.height * w / img.width))),
                             Image.LANCZOS)
        return img
    if img.size != (w, h):
        img = ImageOps.contain(img, (w, h), Image.LANCZOS)
    page = Image.new("L", (w, h), 255)
    page.paste(img, ((w - img.width) // 2, (h - img.height) // 2))
    return page


def test_image(w, h):
    """Border, diagonals, 2 mm ruler on top, block in the bottom-left."""
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, w - 1, h - 1), outline=0)
    d.line((0, 0, w - 1, h - 1), fill=0)
    d.line((0, h - 1, w - 1, 0), fill=0)
    for i in range(0, w, 16):
        d.line((i, 0, i, 8), fill=0)
    d.rectangle((4, h - 20, 20, h - 4), fill=0)
    return img


def ruler_image(w, h):
    """mm ruler across the full width, numbered every 10 mm; shows where
    the printable area ends."""
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    f = load_font(max(10, h // 4))
    for x in range(0, w, DOTS_PER_MM):
        n = x // DOTS_PER_MM
        d.line((x, 0, x, h // 2 if n % 10 == 0 else h // 4 if n % 5 == 0
                else h // 8), fill=0)
        if n % 10 == 0 and n:
            d.text((x - 2, h // 2 + 2), str(n), font=f, fill=0, anchor="ra")
    d.line((0, h - 1, w - 1, h - 1), fill=0)
    return img


# Code 128 bar/space widths for symbol values 0-102, then Start B (104),
# Start C (105) and Stop (106); 103 (Start A) is unused here.
C128 = ("212222 222122 222221 121223 121322 131222 122213 122312 132212 221213 "
        "221312 231212 112232 122132 122231 113222 123122 123221 223211 221132 "
        "221231 213212 223112 312131 311222 321122 321221 312212 322112 322211 "
        "212123 212321 232121 111323 131123 131321 112313 132113 132311 211313 "
        "231113 231311 112133 112331 132131 113123 113321 133121 313121 211331 "
        "231131 213113 213311 213131 311123 311321 331121 312113 312311 332111 "
        "314111 221411 431111 111224 111422 121124 121421 141122 141221 112214 "
        "112412 122114 122411 142112 142211 241211 221114 413111 241112 134111 "
        "111242 121142 121241 114212 124112 124211 411212 421112 421211 212141 "
        "214121 412121 111143 111341 131141 114113 114311 411113 411311 113141 "
        "114131 311141 411131 211412 211214 211232 2331112").split()


def code128(data):
    """-> list of module widths (bar first). Pure digits of even length >= 4
    use code set C (two digits per symbol), everything else code set B."""
    if len(data) >= 4 and len(data) % 2 == 0 and data.isdigit():
        vals = [105] + [int(data[i:i + 2]) for i in range(0, len(data), 2)]
    else:
        if any(not 32 <= ord(c) <= 126 for c in data):
            raise ValueError("Code 128: only printable ASCII is supported")
        vals = [104] + [ord(c) - 32 for c in data]
    vals.append((vals[0] + sum(i * v for i, v in enumerate(vals) if i)) % 103)
    vals.append(106)
    return [int(c) for v in vals for c in C128[v]]


def barcode_image(data, w, h, font=None, text=True):
    """Code 128 with human-readable text, bars scaled by whole dots."""
    widths = code128(data)
    mods = sum(widths) + 20                          # 10-module quiet zones
    scale = w // mods
    if scale < 1:
        raise ValueError("Code 128: %d modules do not fit into %d dots"
                         % (mods, w))
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    bar_h = h - 3
    if text:
        f = load_font(max(10, h // 6), font)
        tb = d.textbbox((0, 0), data, font=f)
        bar_h = h - (tb[3] - tb[1]) - 6
        d.text(((w - (tb[2] - tb[0])) // 2 - tb[0],
                h - (tb[3] - tb[1]) - 2 - tb[1]), data, font=f, fill=0)
    x = (w - sum(widths) * scale) // 2
    for i, m in enumerate(widths):
        if i % 2 == 0:                               # even index = bar
            d.rectangle((x, 2, x + m * scale - 1, bar_h), fill=0)
        x += m * scale
    return img


def qr_matrix(data):
    """-> list of rows of bools, without quiet zone. Uses python3-qrcode,
    falls back to the qrencode program."""
    try:
        import qrcode
        q = qrcode.QRCode(border=0,
                          error_correction=qrcode.constants.ERROR_CORRECT_M)
        q.add_data(data)
        q.make(fit=True)
        return q.get_matrix()
    except ImportError:
        pass
    try:
        out = subprocess.run(["qrencode", "-l", "M", "-m", "0", "-t", "ASCII",
                              "-o", "-", data], check=True,
                             capture_output=True, text=True).stdout
    except FileNotFoundError:
        raise RuntimeError("QR codes need python3-qrcode or qrencode")
    # ASCII: each module is two characters, "#" = dark
    return [[row[i] == "#" for i in range(0, len(row), 2)]
            for row in out.splitlines() if row]


def qr_image(data, w, h):
    """QR code centred, scaled by whole dots, 2-module quiet zone."""
    m = qr_matrix(data)
    n = len(m)
    scale = min(w, h) // (n + 4)
    if scale < 1:
        raise ValueError("QR: %d modules do not fit into %d dots"
                         % (n + 4, min(w, h)))
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    x0, y0 = (w - n * scale) // 2, (h - n * scale) // 2
    for y, row in enumerate(m):
        for x, dark in enumerate(row):
            if dark:
                d.rectangle((x0 + x * scale, y0 + y * scale,
                             x0 + (x + 1) * scale - 1,
                             y0 + (y + 1) * scale - 1), fill=0)
    return img


def to_rows(img, dither=False):
    """Image -> list of bytes rows, MSB = left, 1 = black."""
    mode = Image.FLOYDSTEINBERG if dither else Image.NONE
    bw = ImageOps.invert(img.convert("L").convert("1", dither=mode)
                         .convert("L")).convert("1", dither=Image.NONE)
    nb = (bw.width + 7) // 8
    raw = bw.tobytes()
    return [raw[y * nb:(y + 1) * nb] for y in range(bw.height)]


# ---------- protocol encoding (pure functions, no I/O) ----------------------

def cmd_density(n):
    return b"\x1c\x21" + bytes([max(0, min(16, n))])


def cmd_offset(h=0, v=0):
    """Print offset in printer units; negative values shift the other way."""
    return (b"\x1b\x35" + bytes([h < 0, min(255, abs(h))]) +
            b"\x1b\x34" + bytes([v < 0, min(255, abs(v))]))


def cmd_paper(paper):
    return b"\x1c\x25" + bytes([PAPER[paper] if isinstance(paper, str)
                                else paper])


def cmd_label(rows, width_mm, height_mm):
    return b"\x1b\x29" + struct.pack("<HHHB", rows + 1, round(width_mm * 10),
                                     round(height_mm * 10), 0)


def cmd_row(row):
    """One raster row: leading/trailing zero bytes are skipped."""
    front = len(row) - len(row.lstrip(b"\0"))
    data = row[front:].rstrip(b"\0")
    return b"\x1b\x19\x01" + bytes([len(data) + 1, front]) + data


CMD_ANDROID = b"\x1b\x33"
CMD_FEED0 = b"\x1b\x4a\x00"
CMD_PRINT = b"\x1b\x69"            # print buffer, advance to next label
CMD_RESET = b"\x1b\x41"


CHUNK_ROWS = 4000                  # continuous jobs are split every 500 mm


def encode_job(img, width_mm, height_mm=None, paper="gap", density=6,
               offset=(0, 0), dither=False):
    """Full job for one label, as sent by QLabel (LPBTService.printImage).
    height_mm None -> derived from the image height (continuous paper).
    The label header holds the row count in 16 bits, so long continuous
    images are sent as several labels of CHUNK_ROWS rows each."""
    w = min(img.width, HEAD_DOTS)
    if img.width > w:
        img = img.crop((0, 0, w, img.height))
    rows = to_rows(img, dither)
    if height_mm is None:
        chunks = [rows[i:i + CHUNK_ROWS]
                  for i in range(0, len(rows), CHUNK_ROWS)] or [rows]
    elif len(rows) < 0xFFFF and height_mm * 10 <= 0xFFFF:
        chunks = [rows]
    else:
        raise ValueError("label too long: %d rows" % len(rows))
    out = bytearray()
    out += cmd_density(density) + cmd_offset(*offset) + cmd_paper(paper)
    for part in chunks:
        h = height_mm if height_mm is not None else len(part) / DOTS_PER_MM
        out += CMD_ANDROID + cmd_label(len(part), width_mm, h)
        for r in part:
            out += cmd_row(r)
        out += CMD_FEED0 + CMD_PRINT
    return bytes(out)


# ---------- transports -------------------------------------------------------

class UsbTransport:
    """pyusb on the printer-class interface; detaches usblp if bound."""

    def __init__(self, vid=VID, pid=PID):
        try:
            import usb.core
            import usb.util
        except ImportError:
            raise PrinterError("pyusb is not installed (python3-usb)")
        self.usb = usb
        self.dev = usb.core.find(idVendor=vid, idProduct=pid)
        if self.dev is None:
            raise PrinterError("no USB device %04x:%04x" % (vid, pid))
        try:
            if self.dev.is_kernel_driver_active(0):
                self.dev.detach_kernel_driver(0)
        except (NotImplementedError, usb.core.USBError):
            pass
        usb.util.claim_interface(self.dev, 0)

    def write(self, data):
        for i in range(0, len(data), 4096):
            self.dev.write(0x01, data[i:i + 4096], timeout=10000)

    def read(self, timeout=0.5):
        """All bytes that arrive until `timeout` s of silence."""
        out = b""
        while True:
            try:
                out += bytes(self.dev.read(0x81, 64,
                                           timeout=int(timeout * 1000)))
            except self.usb.core.USBTimeoutError:
                return out

    def close(self):
        self.usb.util.release_interface(self.dev, 0)
        self.usb.util.dispose_resources(self.dev)


class LpTransport:
    """Kernel usblp node; write-only in practice (no replies)."""

    def __init__(self, path):
        self.f = open(path, "wb", buffering=0)

    def write(self, data):
        pos = 0
        while pos < len(data):
            pos += self.f.write(data[pos:pos + 4096])

    def read(self, timeout=0.5):
        return b""

    def close(self):
        self.f.close()


class RfcommTransport:
    """Bluetooth classic SPP, e.g. RfcommTransport("AA:BB:CC:DD:EE:FF").
    RFCOMM channel 1, no pairing needed; replies work as over USB."""

    def __init__(self, addr, channel=1):
        self.s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM,
                               socket.BTPROTO_RFCOMM)
        self.s.connect((addr, channel))

    def write(self, data):
        self.s.sendall(data)

    def read(self, timeout=0.5):
        self.s.settimeout(timeout)
        out = b""
        try:
            while True:
                chunk = self.s.recv(256)
                if not chunk:
                    return out
                out += chunk
        except socket.timeout:
            return out

    def close(self):
        self.s.close()


class FileTransport:
    """Writes the raw byte stream to a file object (e.g. sys.stdout.buffer)."""

    def __init__(self, f):
        self.f = f

    def write(self, data):
        self.f.write(data)

    def read(self, timeout=0.5):
        return b""

    def close(self):
        self.f.flush()


def open_transport(dev=None):
    """None -> USB via pyusb, then /dev/usb/lp*; "-" -> stdout;
    "bt:AA:BB:.." -> RFCOMM; anything else -> device/file path."""
    if dev == "-":
        return FileTransport(sys.stdout.buffer)
    if dev and dev.startswith("bt:"):
        return RfcommTransport(dev[3:])
    if dev:
        return LpTransport(dev)
    try:
        return UsbTransport()
    except PrinterError as e:
        nodes = sorted(glob.glob("/dev/usb/lp*"))
        if nodes:
            return LpTransport(nodes[0])
        raise e


class DebugConsole:
    """Read-only debug UART on the printer board (3.3 V TTL, ~266 kbaud,
    8N1). Logs print parameters on every USB transfer and 'F....' while
    printing. Needs pyserial."""

    BAUD = 266667

    def __init__(self, port="/dev/ttyUSB0", baud=BAUD):
        import serial
        self.s = serial.Serial(port, baud, timeout=0.05)

    def read(self, seconds=1.0):
        """Everything logged within `seconds`, decoded as latin-1."""
        end, out = time.time() + seconds, b""
        while time.time() < end:
            out += self.s.read(4096)
        return out.decode("latin-1")

    def lines(self):
        """Yield log lines forever."""
        buf = b""
        while True:
            buf += self.s.read(4096)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                yield line.rstrip(b"\r").decode("latin-1")

    def close(self):
        self.s.close()


# ---------- printer ---------------------------------------------------------

ERRORS = {0xE1: "error E1", 0xE2: "error E2", 0xE3: "error E3",
          0xE4: "error E4", 0xE5: "error E5", 0xE6: "error E6",
          0xE7: "error E7", 0xE8: "error E8"}


class E210:
    def __init__(self, dev=None, transport=None):
        self.t = transport or open_transport(dev)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self.t.close()

    @staticmethod
    def size_dots(width_mm, height_mm):
        return min(mm(width_mm), HEAD_DOTS), mm(height_mm)

    # raw access
    def write(self, data):
        self.t.write(data)

    def query(self, cmd, timeout=0.5):
        """Send cmd, return the reply without the echoed command bytes
        (or the full reply if it does not start with them)."""
        self.t.read(0.05)                            # drop stale bytes
        self.t.write(cmd)
        r = self.t.read(timeout)
        return r[len(cmd):] if r.startswith(cmd) else r

    # queries
    def status(self):
        """Raw status bytes (ESC 06), currently b"\\x00\\x04" when idle."""
        return self.query(b"\x1b\x06")

    def params(self):
        """ESC 17 -> dict; dpi is certain, the rest is best guess."""
        r = self.query(b"\x1b\x17")
        if len(r) < 7:
            raise PrinterError("no reply to ESC 17: %r" % r)
        return {"dpi": r[0] | r[1] << 8, "raw": r.hex(" ")}

    def model(self):
        return self.query(b"\x1b\x18")

    # settings / motion
    def set_density(self, n):
        self.write(cmd_density(n))

    def set_paper(self, paper):
        self.write(cmd_paper(paper))

    def set_offset(self, h=0, v=0):
        self.write(cmd_offset(h, v))

    def next_label(self):
        """Print/eject: advances to the start of the next label."""
        self.write(CMD_PRINT)

    def feed(self, dots):
        """ESC J n: feed `dots` dot lines (8 per mm), split into <= 255."""
        while dots > 0:
            self.write(b"\x1b\x4a" + bytes([min(255, dots)]))
            dots -= 255

    def reset(self):
        self.write(CMD_RESET)

    # printing
    def print_image(self, img, width_mm, height_mm=None, paper="gap",
                    density=6, copies=1, offset=(0, 0), dither=False):
        job = encode_job(img, width_mm, height_mm, paper, density, offset,
                         dither)
        for _ in range(copies):
            self.write(job)
        err = self.t.read(0.1)
        for b in err:
            if b in ERRORS:
                raise PrinterError(ERRORS[b])
        return len(job) * copies
