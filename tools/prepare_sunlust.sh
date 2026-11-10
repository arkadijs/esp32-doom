#!/usr/bin/env bash
# Produce a flash-ready five-map Sunlust pack from legally supplied WADs.
set -euo pipefail
if [[ $# -lt 4 || $# -gt 5 ]]; then
    echo "Usage: $0 doom2.wad sunlust.wad prboom-plus.wad output-directory [maps=5]" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
iwad=$1
pwad=$2
support=$3
output=$4
maps=${5:-5}
python3 - "$iwad" "$pwad" "$support" "$output" <<'PYGUARD'
from pathlib import Path
import sys
inputs = {Path(p).resolve() for p in sys.argv[1:4]}
for name in ('doom2.wad', 'prboom-plus.wad'):
    if (Path(sys.argv[4]) / name).resolve() in inputs:
        raise SystemExit('Choose an output directory distinct from the input WADs')
PYGUARD
mkdir -p "$output/source"
python3 "$script_dir/pack_sunlust.py" --iwad "$iwad" --pwad "$pwad" \
    --support "$support" --maps "$maps" --output "$output/source/doom2.wad"
python3 "$script_dir/test_sunlust_pack.py" "$output/source/doom2.wad" "$pwad" --iwad "$iwad"
python3 "$script_dir/build_flash_textures.py" "$output/source/doom2.wad" "$output/doom2.wad"
cp "$output/source/prboom-plus.wad" "$output/prboom-plus.wad"
python3 "$script_dir/test_flash_textures.py" "$output/source/doom2.wad" \
    "$output/doom2.wad" "$output/host/composites.raw"
