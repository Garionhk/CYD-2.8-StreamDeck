#!/usr/bin/env python3
"""
fake_companion.py -- a stand-in for Bitfocus Companion's Satellite API.

Lets you test the deck without installing Companion. It listens on TCP 16622,
does the same handshake Companion does, sends every key the deck asked for
(KEYS_TOTAL: 32 for 8x4) as RGB888 bitmaps -- a colour plus the key number --
and logs every KEY-PRESS.

Two flavours, matching the two kinds of real Companion:
  default     API 1.14: bitmaps at the size ADD-DEVICE BITMAPS=n asked for,
              values quoted (Companion 4.x and late 3.x)
  --legacy    API 1.4.0: always 72x72, values unquoted (Companion 3.0) Pressing a
key toggles it between its colour and white, the way a Companion button with
a "pressed" style would.

    python3 tools/fake_companion.py                # bitmaps
    python3 tools/fake_companion.py --text-only    # no BITMAP: tests the fallback
    python3 tools/fake_companion.py --legacy       # behave like Companion 3.0

While it runs, type a number 10-100 and Enter to send BRIGHTNESS, or "c" to
send KEYS-CLEAR. Point the deck's Companion IP at this computer.

Only the standard library is used.
"""
import argparse
import base64
import socket
import sys
import threading

PORT = 16622
SIDE = 72

COLOURS = [
    (220, 50, 50), (240, 140, 30), (230, 200, 40), (60, 180, 75),
    (30, 160, 160), (40, 110, 220), (110, 70, 200), (200, 60, 160),
    (120, 120, 120), (150, 90, 50), (20, 90, 60), (40, 40, 90),
]

# 3x5 digits, one string per row.
DIGITS = {
    "0": ["111", "101", "101", "101", "111"], "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"], "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"], "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"], "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"], "9": ["111", "101", "111", "001", "111"],
}


def bitmap(key: int, pressed: bool, size: int = SIDE) -> bytes:
    """size x size RGB888: colour tile with the 1-based key number, drawn at
    72x72 and resampled (nearest pixel) to the size asked for."""
    base = COLOURS[key % len(COLOURS)]
    bg = (255, 255, 255) if pressed else base
    fg = base if pressed else (255, 255, 255)
    px = [[bg] * SIDE for _ in range(SIDE)]
    label = str(key + 1)
    scale = 6
    w = len(label) * 4 * scale - scale
    x0, y0 = (SIDE - w) // 2, (SIDE - 5 * scale) // 2
    for i, ch in enumerate(label):
        for r, row in enumerate(DIGITS[ch]):
            for c, bit in enumerate(row):
                if bit == "1":
                    for dy in range(scale):
                        for dx in range(scale):
                            px[y0 + r * scale + dy][x0 + (i * 4 + c) * scale + dx] = fg
    # Darker 2 px frame, so the tile edge is visible against black.
    for i in range(SIDE):
        for j in (0, 1, SIDE - 2, SIDE - 1):
            px[i][j] = px[j][i] = tuple(v // 2 for v in base)
    if size != SIDE:
        px = [[px[y * SIDE // size][x * SIDE // size] for x in range(size)] for y in range(size)]
    return bytes(v for row in px for p in row for v in p)


def hexcol(rgb):
    return "#%02x%02x%02x" % rgb


class Session:
    def __init__(self, conn, addr, text_only, legacy):
        self.conn, self.addr, self.text_only, self.legacy = conn, addr, text_only, legacy
        self.size = SIDE
        self.device = None
        self.pressed = []
        self.lock = threading.Lock()

    def send(self, line: str):
        with self.lock:
            self.conn.sendall((line + "\n").encode())

    def send_key(self, key: int):
        p = self.pressed[key]
        q = (lambda v: v) if self.legacy else (lambda v: f'"{v}"')   # 3.0 does not quote
        parts = [f'KEY-STATE DEVICEID={q(self.device)} KEY={key} TYPE={q("BUTTON")} PRESSED={int(p)}']
        if not self.text_only:
            parts.append("BITMAP=" + q(base64.b64encode(bitmap(key, p, self.size)).decode()))
        bg = (255, 255, 255) if p else COLOURS[key % len(COLOURS)]
        parts.append(f'COLOR={q(hexcol(bg))} TEXTCOLOR={q(hexcol((0, 0, 0) if p else (255, 255, 255)))}')
        text = f"Key {key + 1}\nfake"
        parts.append("TEXT=" + q(base64.b64encode(text.encode()).decode()))
        self.send(" ".join(parts))

    def run(self):
        print(f"[{self.addr[0]}] connected")
        if self.legacy:
            self.send("BEGIN CompanionVersion=fake-3.0 ApiVersion=1.4.0")
        else:
            self.send('BEGIN CompanionVersion="fake" ApiVersion="1.14.0"')
        buf = b""
        while True:
            data = self.conn.recv(4096)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.handle(line.decode(errors="replace").strip())
        print(f"[{self.addr[0]}] disconnected")

    def handle(self, line: str):
        if not line:
            return
        cmd, _, rest = line.partition(" ")
        params = parse(rest)
        if cmd == "PING":
            self.send("PONG " + rest)
        elif cmd == "ADD-DEVICE":
            self.device = params.get("DEVICEID", "?")
            print(f"[{self.addr[0]}] ADD-DEVICE {self.device}: "
                  f"{params.get('KEYS_TOTAL')} keys, {params.get('KEYS_PER_ROW')} per row, "
                  f"bitmaps {params.get('BITMAPS')} px, name {params.get('PRODUCT_NAME')!r}")
            self.send(f'ADD-DEVICE OK DEVICEID="{self.device}"')
            self.pressed = [False] * int(params.get("KEYS_TOTAL", 32))
            # API >= 1.5 honours BITMAPS=<size>; 3.0 always sends 72.
            b = params.get("BITMAPS", "")
            self.size = int(b) if (b.isdigit() and int(b) >= 5 and not self.legacy) else SIDE
            for k in range(len(self.pressed)):
                self.send_key(k)
        elif cmd == "KEY-PRESS":
            key = int(params.get("KEY", -1))
            down = params.get("PRESSED") not in ("0", "false")
            print(f"[{self.addr[0]}] KEY-PRESS key {key + 1} {'down' if down else 'up'}")
            if down and 0 <= key < len(self.pressed):
                self.pressed[key] = not self.pressed[key]
                self.send_key(key)
        else:
            print(f"[{self.addr[0]}] {line[:120]}")


def parse(rest: str) -> dict:
    out, i = {}, 0
    while i < len(rest):
        while i < len(rest) and rest[i] == " ":
            i += 1
        j = rest.find("=", i)
        if j < 0:
            break
        key = rest[i:j]
        if j + 1 < len(rest) and rest[j + 1] == '"':
            end = rest.find('"', j + 2)
            end = len(rest) if end < 0 else end
            out[key] = rest[j + 2:end]
            i = end + 1
        else:
            end = rest.find(" ", j + 1)
            end = len(rest) if end < 0 else end
            out[key] = rest[j + 1:end]
            i = end
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--text-only", action="store_true", help="send no BITMAP (tests the text fallback)")
    ap.add_argument("--legacy", action="store_true", help="behave like Companion 3.0 (API 1.4, 72x72, unquoted)")
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)   # logs show up even when piped

    sessions = []
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen()
    host = socket.gethostbyname(socket.gethostname()) if sys.platform != "darwin" else "this Mac's IP"
    print(f"fake Companion listening on TCP {args.port} -- set the deck's Companion IP to {host}")
    print("type 10-100 + Enter for BRIGHTNESS, 'c' for KEYS-CLEAR, Ctrl-C to quit")

    def accept():
        while True:
            conn, addr = srv.accept()
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s = Session(conn, addr, args.text_only, args.legacy)
            sessions.append(s)
            threading.Thread(target=s.run, daemon=True).start()

    threading.Thread(target=accept, daemon=True).start()
    try:
        for line in sys.stdin:
            line = line.strip()
            for s in list(sessions):
                if not s.device:
                    continue
                try:
                    if line.isdigit():
                        s.send(f'BRIGHTNESS DEVICEID="{s.device}" VALUE={int(line)}')
                    elif line == "c":
                        s.send(f'KEYS-CLEAR DEVICEID="{s.device}"')
                except OSError:
                    sessions.remove(s)
        threading.Event().wait()   # stdin closed (run in the background): keep serving
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
