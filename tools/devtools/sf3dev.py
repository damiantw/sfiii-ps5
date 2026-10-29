#!/usr/bin/env python3
"""Client for the sfiii-ps5 debug server (dev builds). Same calls as saturnrecomp.agent.Run
(step, pad, read, write, frame, vblank), so sf3test / sf3-story code drives a console run too.

  sf3dev.py ping | info | pause | run | step N | pad BUTTONS | read ADDR LEN | write ADDR HEX
  sf3dev.py shot OUT.png | save PATH | load PATH | log [N]
URL: --url or SF3DEV_URL (default tcp://192.168.1.70:9300); token: SF3DEV_TOKEN."""
import argparse, os, socket, struct, sys, zlib


class DevError(Exception):
    pass


class Frame:
    def __init__(self, w, h, rgb):
        self.width, self.height, self.rgb = w, h, rgb

    def pixel(self, x, y):
        i = (y * self.width + x) * 3
        return tuple(self.rgb[i:i + 3])

    def save_png(self, path):
        raw = b"".join(b"\0" + self.rgb[y * self.width * 3:(y + 1) * self.width * 3] for y in range(self.height))
        chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Client:
    def __init__(self, url=None, token=None, timeout=30):
        url = url or os.environ.get("SF3DEV_URL", "tcp://192.168.1.70:9300")
        host, port = url.removeprefix("tcp://").rsplit(":", 1)
        self.s = socket.create_connection((host, int(port)), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.f = self.s.makefile("rb")
        hello = self._line()
        if not hello.startswith("hello sf3dev"):
            raise DevError("not a debug server: " + hello)
        token = token or os.environ.get("SF3DEV_TOKEN")
        if token:
            self._ask("auth " + token)
        self.vblank = None

    def _line(self):
        l = self.f.readline()
        if not l:
            raise DevError("server closed the connection")
        return l.decode().rstrip("\n")

    def _ask(self, cmd):
        self.s.sendall((cmd + "\n").encode())
        r = self._line()
        if r.startswith("error"):
            raise DevError(r)
        return r

    def _vblank(self, r):
        if not r.startswith("vblank "):
            raise DevError("expected vblank, got " + r)
        self.vblank = int(r.split()[1])
        return self.vblank

    # agent.Run-compatible
    def pause(self):
        self.paused = True
        self._ask("pause")
        return self._vblank(self._line())

    def step(self, n=1):
        return self._vblank(self._ask(f"step {n}"))

    def pad(self, buttons="", pad=1):
        self._ask(f"pad {pad}.{buttons}" if pad != 1 else f"pad {buttons}")

    def read(self, addr, n):
        return bytes.fromhex(self._ask(f"read {addr:X} {n}").split()[1] if n else "")

    def read32(self, addr):
        return int.from_bytes(self.read(addr, 4), "big")

    def write(self, addr, data):
        self._ask(f"write {addr:X} {bytes(data).hex()}")

    def frame(self):
        _, w, h = self._ask("frame").split()
        w, h = int(w), int(h)
        buf = self.f.read(w * h * 3)
        return Frame(w, h, buf)

    # console extras
    def ping(self):
        return int(self._ask("ping").split()[1])

    def info(self):
        return self._ask("info")[5:]

    def run(self):
        self.paused = False
        self._ask("run")

    def save(self, path):
        self._ask("save " + path)

    def load(self, path, wait=True):
        """Loads a native state live, at the game's next safe moment. True once it has loaded."""
        self._ask("load " + path)
        if not wait:
            return None
        import time
        for _ in range(400):                       # the load happens as the game runs: step it when paused
            st = self._ask("loaded").split()
            if st[1] == "done":
                return st[2] == "1"
            if getattr(self, "paused", False): self.step(1)
            else: time.sleep(0.025)
        return False

    def log(self, n=100):
        k = int(self._ask(f"log {n}").split()[1])
        return [self._line() for _ in range(k)]

    def close(self):
        try:
            self._ask("detach")
        except (DevError, OSError):
            pass
        self.s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    c = Client(a.url)
    x = a.args
    if a.cmd == "ping": print(c.ping())
    elif a.cmd == "info": print(c.info())
    elif a.cmd == "pause": print(c.pause())
    elif a.cmd == "run": c.run()
    elif a.cmd == "step": c.pause(); print(c.step(int(x[0]) if x else 1)); c.run()
    elif a.cmd == "pad": c.pad(x[0] if x else "")
    elif a.cmd == "read": print(c.read(int(x[0], 16), int(x[1])).hex())
    elif a.cmd == "write": c.write(int(x[0], 16), bytes.fromhex(x[1]))
    elif a.cmd == "shot": c.pause(); c.frame().save_png(x[0]); c.run()
    elif a.cmd in ("save", "load"): getattr(c, a.cmd)(x[0])
    elif a.cmd == "log": print("\n".join(c.log(int(x[0]) if x else 100)))
    else: sys.exit("unknown command " + a.cmd)
    c.close()


if __name__ == "__main__":
    main()
