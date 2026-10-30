#!/usr/bin/env python3
"""Input-to-frame latency on the console through the debug server (MIT).

Taps a button N times and, for each, times (wall clock and vblanks) until the game's native
frame changes. That is the emulation side: pad read -> game reacts -> field composed. The display
queue adds (swapchain images - 1) refreshes on top; the console log line "display: ... images N"
gives N. With --rp, also asks ps5ctl for the end-to-end figure (pad -> Remote Play video).

  latency.py [--button RIGHT] [--n 20] [--rp]
Use on a screen where the button visibly moves something (a cursor or the hero), unpaused.
"""
import argparse, hashlib, json, statistics, subprocess, sys, time, os
sys.path.insert(0, os.path.dirname(__file__))
import sf3dev

def h(fr): return hashlib.md5(bytes(fr.rgb if hasattr(fr, "rgb") else fr.buf)).digest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--button", default="RIGHT"); ap.add_argument("--back", default="LEFT")
    ap.add_argument("--n", type=int, default=20); ap.add_argument("--rp", action="store_true")
    a = ap.parse_args()
    c = sf3dev.Client()
    ms, vb = [], []
    for i in range(a.n):
        btn = a.button if i % 2 == 0 else a.back
        # settle: wait for two identical frames so idle animation does not count as a reaction
        prev = h(c.frame())
        for _ in range(60):
            cur = h(c.frame())
            if cur == prev: break
            prev = cur
        v0 = c.ping(); t0 = time.monotonic()
        c.pad(btn)
        while time.monotonic() - t0 < 1.0:
            if h(c.frame()) != prev:
                ms.append((time.monotonic() - t0) * 1000); vb.append(c.ping() - v0); break
        time.sleep(0.3)
    out = {"button": a.button, "samples": len(ms)}
    if ms:
        out.update(ms_median=round(statistics.median(ms), 1), ms_max=round(max(ms), 1),
                   vblanks_median=statistics.median(vb), vblanks=vb)
    if a.rp:
        r = subprocess.run(["/workspace/ps5ctl/ps5ctl", "press", a.button, "--hold", "250", "--measure"],
                           capture_output=True, text=True)
        out["remote_play"] = r.stdout.strip()
    print(json.dumps(out, indent=1))

if __name__ == "__main__":
    main()
