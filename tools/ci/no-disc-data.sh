#!/bin/sh
# Fail if a tracked file looks like disc data or a translation package (outside the submodules).
# The approved title art (ps5/sce_sys/icon0, pic0, pic1) may be larger than 1 MB.
set -e
bad=$(git ls-files | grep -Ei '\.(bin|cue|chd|iso|img|ccd|mdf|7z|exe|zip|bak)$' | grep -v '^external/' || true)
big=$(git ls-files -z | xargs -0 -I{} sh -c 'test -f "{}" && [ $(stat -c%s "{}") -gt 1048576 ] && echo "{}"' | grep -Ev '^ps5/sce_sys/(icon0|pic0|pic1)\.png$' || true)
if [ -n "$bad$big" ]; then echo "disc-like or large files tracked:"; echo "$bad"; echo "$big"; exit 1; fi
echo "no disc data: ok"
