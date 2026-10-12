#!/bin/sh
# SPDX-License-Identifier: MIT
# Regenerates the compiled test-monochord fixture used by the C++ loader test "B2-R7 ...".
# Run from the repository root after any change to the body compiler's numerics:
#   sh compiler/tests/data/b2/regenerate.sh
set -eu
out=compiler/tests/data/b2/compiled-test-monochord
rm -rf "$out"
uv run python -m pme_compiler.body instruments/examples/test-monochord/instrument.json \
  -o "$out" --f-max 400 --mesh-size 0.1 --grid-order 1
