#!/usr/bin/env bash
# Flash both user-supplied assets in one reset/download cycle.
set -euo pipefail
wad_dir=${DOOMWADDIR:?Set DOOMWADDIR to the directory containing doom2.wad and prboom-plus.wad}
serial_port=${1:-${ESPPORT:-/dev/ttyACM0}}
python - "$wad_dir/doom2.wad" "$wad_dir/prboom-plus.wad" <<'PY'
from pathlib import Path
import sys
for file, limit in zip(sys.argv[1:], (14848 * 1024, 448 * 1024)):
    p = Path(file)
    size = p.stat().st_size
    if size < 12 or size > limit:
        raise SystemExit(f'{p}: {size} bytes; expected a WAD fitting {limit} bytes')
    with p.open('rb') as f:
        if f.read(4) not in (b'IWAD', b'PWAD'):
            raise SystemExit(f'{p}: invalid WAD header')
PY
python -m esptool --chip esp32s3 --port "$serial_port" --baud 921600 \
    --after hard-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
    0x100000 "$wad_dir/doom2.wad" 0xF80000 "$wad_dir/prboom-plus.wad"
