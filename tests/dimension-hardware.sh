#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
test "$#" -eq 2 || { echo "usage: $0 DRIVER_DIRECTORY NEW_OUTPUT_DIRECTORY" >&2; exit 2; }
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/require-hw-guard.sh"
require_hw_guard
driver_dir=$(realpath -- "$1")
test -f "$driver_dir/v4l2_request_drv_video.so"
output=$2
mkdir -- "$output"
output=$(realpath -- "$output")
export LIBVA_DRIVERS_PATH="$driver_dir"
export LIBVA_DRIVER_NAME=v4l2_request
export LIBVA_V4L2_DIAG=json
# The exported ioctl symbol observes calls from the dynamically loaded driver.
# Keep assertions enabled independently of production-driver build options.
${CC:-cc} -Wall -Wextra -Werror -O2 -UNDEBUG -Wl,--export-dynamic \
    "$script_dir/dimension-probe.c" -o "$output/dimension-probe" \
    $(pkg-config --cflags --libs libva libva-drm) -ldl
sha256sum "$driver_dir/v4l2_request_drv_video.so" "$output/dimension-probe" \
    > "$output/binaries.sha256"
"$output/dimension-probe" > "$output/results.jsonl" 2> "$output/driver.log"
python3 - "$output/results.jsonl" <<'PY'
import json, sys
from pathlib import Path
rows = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines()]
assert len(rows) == 31
assert rows[-1]['case'] == 'summary' and rows[-1]['passed'] is True
assert rows[-1]['query_calls'] > 0 and rows[-1]['dimension_enumerations'] > 0
assert rows[-1]['control_submissions'] == rows[-1]['request_queues'] == 0
assert sum(r.get('case') == 'attributes' for r in rows) == 2
for profile in (0, 2):
    for case in ('context', 'picture'):
        selected = [r for r in rows if r.get('profile') == profile and r['case'] == case]
        assert len(selected) == 7
        assert sum(r['accepted'] for r in selected) == 2
print('VP9 profile 0/2: truthful attributes and 28 admission cases passed; zero submissions')
PY
