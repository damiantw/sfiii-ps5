#!/usr/bin/env python3
"""Input latency on the console, for A/B runs of the latency settings (MIT).

  latency.py rp   [--button DOWN --back UP --n 20]  pad -> Remote Play video change (ps5ctl --measure).
      Use on a host-drawn screen that reacts on the next frame (the launcher list). The stream adds a
      constant, so compare medians between builds or settings rather than reading them as absolute.
  latency.py host [--host 192.168.1.70]             the console's own figures from /data/sfiii/stderr.txt:
      "display: ... images N" (FIFO queue: up to N-1 refreshes between present and scan-out) and
      "pad: game sample age ..." (how old the controller read is when the game samples it).
The emulated game's own reaction time (fields from SMPC read to a changed picture) is the game's and
is the same on Beetle; the debug server's live state load is not frame-exact, so it is not measured here.
"""
import argparse, json, re, statistics, subprocess, sys, time, urllib.request

def rp(a):
    ms = []
    for i in range(a.n):
        b = a.button if i % 2 == 0 else a.back
        r = subprocess.run(["/workspace/ps5ctl/ps5ctl", "press", b, "--hold", "250", "--measure"],
                           capture_output=True, text=True)
        try:
            v = json.loads(r.stdout.strip().splitlines()[-1]).get("press_to_visible_change_ms")
            if v is not None: ms.append(float(v))
        except Exception:
            print(r.stdout, r.stderr, file=sys.stderr)
        time.sleep(0.6)
    return {"mode": "rp", "samples": ms, "median_ms": statistics.median(ms) if ms else None,
            "min_ms": min(ms) if ms else None}

def host(a):
    t = urllib.request.urlopen(f"ftp://{a.host}:2121/data/sfiii/stderr.txt", timeout=20).read().decode(errors="replace")
    disp = re.findall(r"display: .*images (\d+)", t)
    pads = re.findall(r"pad: game sample age avg ([\d.]+) ms \(VBlank read ([\d.]+) ms\)(.*)", t)
    return {"mode": "host", "swapchain_images": int(disp[-1]) if disp else None,
            "pad_sample_age_ms": [float(p[0]) for p in pads[-5:]],
            "pad_vblank_read_age_ms": [float(p[1]) for p in pads[-5:]],
            "fresh_read": bool(pads) and "off" not in pads[-1][2]}

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["rp", "host"])
    ap.add_argument("--button", default="DOWN"); ap.add_argument("--back", default="UP")
    ap.add_argument("--n", type=int, default=20); ap.add_argument("--host", default="192.168.1.70")
    a = ap.parse_args()
    print(json.dumps(rp(a) if a.mode == "rp" else host(a), indent=1))
