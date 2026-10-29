#!/usr/bin/env python3
"""Runs the fake-machine server and checks every command through the client."""
import subprocess, sys, time, os
sys.path.insert(0, os.path.dirname(__file__))
import sf3dev
srv = subprocess.Popen([sys.argv[1], "9377"])
try:
    time.sleep(0.3)
    c = sf3dev.Client("tcp://127.0.0.1:9377")
    assert c.ping() >= 0 and "PPSA93303" in c.info()
    v = c.pause()
    assert c.step(5) == v + 5
    assert c.read(0, 1)[0] == (v + 5) & 0xFF   # the game wrote before the poll
    c.write(0x100, b"\x12\x34"); assert c.read(0x100, 2) == b"\x12\x34"
    c.pad("A+START")
    f = c.frame(); assert (f.width, f.height, len(f.rgb)) == (4, 2, 24)
    c.save("/tmp/x.state")
    try: c.load("/nope"); raise SystemExit("load should fail")
    except sf3dev.DevError as e: assert "no such state" in str(e)
    assert c._ask("loaded") == "loaded done 1"
    assert any("listening" in l for l in c.log(10))
    c.run(); time.sleep(0.05); assert c.ping() > v + 5
    c.close()
    c = sf3dev.Client("tcp://127.0.0.1:9377"); assert c.ping() > 0; c.close()   # reconnect after detach
    print("devtools: all checks passed")
finally:
    srv.kill()
