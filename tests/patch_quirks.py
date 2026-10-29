#!/usr/bin/env python3
"""Differential test: the C++ metadata.json / DFR readers against sfiii-native's Python sf3patch.

usage: tests/patch_quirks.py SFIII_NATIVE_CHECKOUT BUILD_DIR
Writes generated inputs to a temp folder; no game data is involved.
"""
import os, subprocess, sys, tempfile

sys.path.insert(0, sys.argv[1])
from tools.sf3patch import dfr, netjson  # noqa: E402

EXE = os.path.join(sys.argv[2], "patch_quirks")

META = [
    b'{"category":"General","text":"A","enabled":true,"priority":5}',
    b"{category:'qol', text:'B', enabled:'True', priority:040,}",
    b'// c\n{"category":"mods","text":12.5,"enabled":1,"priority":"  7 "}',
    b'{"category":"mods","text":null,"enabled":false,"priority":2.5}',
    b'{"category":"mods","text":"x","enabled":false,"priority":3.5}',
    b'{"category":"mods","text":"x","enabled":false,"priority":009}',
    b'{"category":"mods","text":"x","enabled":false,"priority":0x1F}',
    b'{"category":"mods","text":"x","enabled":false,"priority":null}',
    b'{"category":"mods","text":"x","enabled":false}',
    b'{"category":{"a":[1,2.0,"\\u00e9"]},"text":"x","enabled":false}',
    b'{"category":"BugFixes","text":"x","enabled":"yes"}',
    b'\xef\xbb\xbf{"category":"General","text":"\xc3\xa9","enabled":true,"text":"dup"}',
    b'\xff\xfe' + '{"category":"qol","text":"u16","enabled":true}'.encode("utf-16-le"),
    b'{"category":"qol","text":"x","enabled":true,"priority":99999999999}',
    b'{"category":"qol","text":1e3,"enabled":0.0,"priority":-1}',
    b'{"category":"qol","text":"x","enabled":true} trailing',
    b'/* unterminated',
    b'[1,2]',
]
DFR = [
    b"10,1011,AABB\n",
    b"; comment\n0,00,FF ; tail\r\n3,0304,0000\r4,,DEAD\n",
    b"40,,0102\n",
    b"50,,0102\n",
    b"8,\n9,09,00\n",
    b"100,\n",
    b"3e,3E3F,1112\n",
    b"5,00,11\n",
    b"5,0506,11\n",
    b"5,050,112\n",
    b"zz,00,11\n",
    b"5\n",
    b"5, 0 5 , 1 1\n",
    b"\xef\xbb\xbf1,01,02\n",
]


def py_meta(data):
    try:
        doc = netjson.loads(netjson.read_text(data))
        if not isinstance(doc, dict):
            raise netjson.JsonError("x")
        g = lambda k: doc.get(k, netjson.MISSING)
        out = ["category=%s" % netjson.category_string(g("category")).lower()]
        t = netjson.to_net_string(g("text"))
        out.append("text=%s" % ("<None>" if t is None else t))
        out.append("enabled=%d" % netjson.to_net_bool(g("enabled")))
        pr = g("priority")
        if pr is None:
            raise netjson.JsonError("x")
        out.append("priority=%d" % (9999 if pr is netjson.MISSING else netjson.to_net_int(pr)))
        return "\n".join(out) + "\n"
    except (netjson.JsonError, UnicodeError, ValueError, OverflowError):
        return "error\n"


def py_dfr(data):
    try:
        p = dfr.parse(data, "t")
    except (dfr.DFRError, ValueError):
        return "parse error\n"
    kinds = {"replace": 0, "append": 1, "truncate": 2}
    out = ["%d %d %d %s %s" % (o.line, o.offset, kinds[o.kind], o.old.hex(), o.new.hex()) for o in p.ops]
    try:
        r = dfr.apply(p, bytes(range(64)), "f")
        out.append("applied %d:%s" % (len(r), r.hex()))
    except dfr.DFRError as e:
        out.append("apply error: %s" % e)
    return "\n".join(out) + "\n"


def main():
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for kind, cases, ref in (("meta", META, py_meta), ("dfr", DFR, py_dfr)):
            for i, data in enumerate(cases):
                f = os.path.join(tmp, "%s%d" % (kind, i))
                open(f, "wb").write(data)
                got = subprocess.run([EXE, kind, f], stdout=subprocess.PIPE, text=True).stdout
                want = ref(data)
                if got != want:
                    bad += 1
                    print("MISMATCH %s %d %r\n  python: %r\n  c++:    %r" % (kind, i, data, want, got))
    print("%d mismatches" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
