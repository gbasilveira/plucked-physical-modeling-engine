# SPDX-License-Identifier: MIT
"""Smoke test for the Python package skeleton."""

import pme_compiler


def test_p0_1_t2_empty_suite() -> None:
    """P0.1-T2: the package imports and pytest runs the (otherwise empty) suite."""
    assert isinstance(pme_compiler.__version__, str)
    assert pme_compiler.__version__ != ""
