# SPDX-License-Identifier: MIT
"""Build the traceability matrix (V1-R1 to V1-R4).

    python3 tools/trace/build_matrix.py --junit <dir> --out <dir> [--job-results <json>]
        [--known-gaps tools/trace/known-gaps.json] [--fail-on-gaps]

Writes ``traceability.html``, ``traceability.json`` and ``summary.md`` into ``--out`` and prints the
summary. See docs/dev/traceability.md for the options and the definitions.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from trace_matrix.cli import main

if __name__ == "__main__":
    sys.exit(main())
