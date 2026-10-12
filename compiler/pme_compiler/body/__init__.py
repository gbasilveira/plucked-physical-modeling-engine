# SPDX-License-Identifier: MIT
"""Plate and body FEM compiler (B2).

Modules: ``geometry`` (inputs, moisture law), ``mesh`` (Gmsh), ``fem`` (scikit-fem assembly),
``eigen`` (windowed shift-invert solve), ``compile`` (``compile_plate``), ``modal_blob`` (blob
layout 1, shared with B3 and B4), ``cli``. Importing this package imports neither Gmsh nor
scikit-fem.
"""
