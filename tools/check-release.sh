#!/bin/sh
# Refuse a release ELF that carries the debug server.
set -e
if nm -C "$1" 2>/dev/null | grep -q 'devtools_'; then echo "release check: $1 has devtools_ symbols (dev build)"; exit 1; fi
echo "release check: $1 ok"
