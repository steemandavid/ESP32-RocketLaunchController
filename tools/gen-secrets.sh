#!/bin/bash
# Generate components/rlc_common/include/rlc_secrets.h with fresh random keys.
#
# The file is gitignored and must never be committed (bug #20). Both units must
# hold the same keys, so generate ONCE and flash both from the same tree.
#
#   ./tools/gen-secrets.sh          # refuses to clobber an existing file
#   ./tools/gen-secrets.sh --force  # rotate, overwriting the current keys

set -euo pipefail
cd "$(dirname "$0")/.."

OUT="components/rlc_common/include/rlc_secrets.h"

if [ -f "$OUT" ] && [ "${1:-}" != "--force" ]; then
    echo "ERROR: $OUT already exists."
    echo
    echo "Rotating keys means reflashing BOTH units from the same tree, or they"
    echo "cannot talk at all — ESP-NOW encryption fails before the firmware"
    echo "version check, so a half-flashed pair gives no diagnostic, just silence."
    echo
    echo "Re-run with --force if that is what you want."
    exit 1
fi

python3 - "$OUT" <<'PY'
import os, sys, datetime

out = sys.argv[1]

def block(name):
    b = os.urandom(16)
    rows = []
    for i in (0, 8):
        row = ", ".join("0x%02X" % x for x in b[i:i+8])
        rows.append(row)
    # Every line inside the macro needs a continuation, including the last.
    return ("#define %s  { \\\n"
            "    %s, \\\n"
            "    %s  \\\n"
            "}\n") % (name, rows[0], rows[1])

now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

with open(out, "w") as f:
    f.write("/**\n"
            " * RLC Shared Secrets — GENERATED, DO NOT COMMIT\n"
            " *\n"
            " * Written by tools/gen-secrets.sh on %s.\n"
            " * Gitignored per bug #20. Both units must hold these same values.\n"
            " */\n\n"
            "#pragma once\n\n" % now)
    f.write(block("ESPNOW_PMK") + "\n")
    f.write(block("ESPNOW_LMK") + "\n")
    f.write(block("CMD_INTEGRITY_KEY"))
PY

chmod 600 "$OUT"
echo "Wrote $OUT (mode 600)."
echo "Keys are NOT printed here on purpose. Flash BOTH units from this tree."
