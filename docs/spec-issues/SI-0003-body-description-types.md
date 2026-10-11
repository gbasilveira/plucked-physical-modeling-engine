# SI-0003 Body and bridge-point description types: coordinate frame, plate geometry, rim, f_split default

Status: open · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-API "Descriptions" (`BodyParams`, `BridgePointDesc`, `ChannelDesc`, `Defaults`, `Point`); SPEC-BODY "Plate model", "Local bridge model", "Radiation weights"; SPEC-INST "Piano defaults"

## Failing test
None yet. Found in the P0.4 code review. B1-R1 requires the instrument JSON Schema to mirror the SPEC-API description structs, and B2-R1 requires geometry the structs cannot hold, so B1 and B2 will hit this first.

## Evidence
| Item | SPEC says | Provisional choice in interfaces-v1 (2daee85) | Problem |
| --- | --- | --- | --- |
| `Point` frame | Nothing | x along the grain, y across, z normal towards the strings | Contradicts SPEC-BODY's grain angle θ\_g: if x is the grain axis, θ\_g is always 0. A piano soundboard's grain runs diagonally to the case. |
| Plate thickness | h(x, y) | `PlateParams::thickness`, one uniform value | Cannot represent thinned (diaphragmatic) soundboards; B2-R1 asks for a thickness map |
| Plate outline | Implicit | `lengthX`, `lengthY` (rectangle) | B2-R1 asks for an outline polygon and bridges as polylines |
| Rim support | "elastically supported rim", no symbol or unit | `rimStiffness`, N/m per m of rim | Symbol, unit and whether rotational stiffness is included are undefined |
| `RibDesc` | "ribs and bridges as attached beams" | start, end, width, height, youngs, density | Bridges are polylines, not straight beams; no grain/wood reference |
| `BridgePointDesc` | Named only | position, θ\_b, G\_h | Adequate for SPEC-BRG; no per-point M\_b or f\_split override (acceptable) |
| `ChannelDesc` | Named only | name, position R\_c | No gain (see SI-0007) and no lid/case correction filter (SPEC-BODY MAY) |
| `Defaults::localBridgeFSplit` | No default anywhere in SPEC | 2000 Hz | A numerics choice with no basis in SPEC |

## Proposed correction
1. **Frame (documentation, no behaviour change).** SPEC-BODY "Plate model" gains: "Points are given in the body frame: x and y span the plate's mid-surface in the instrument's case frame defined by the body outline; the grain (L axis) lies at angle θ\_g from +x; z is normal to the plate and points towards the strings, matching the +y polarisation axis. Radiation channels use the same frame." The `Point` comment in `types.hpp` changes accordingly.
2. **Plate geometry (physics input; human review).** SPEC-BODY "Plate model" lists the geometry inputs B2-R1 already names: outline polygon, thickness map h(x, y) given on a regular grid over the outline's bounding box and bilinearly interpolated (uniform when the grid is empty), ribs as straight beams, bridges as polylines with a rectangular section, rim support as a translational stiffness per unit rim length k\_rim (N/m²), with an optional rotational stiffness per unit length k\_rim,θ (N·m/rad per m) that MAY be used.
3. **f\_split default (numerics; human review).** Keep 2 kHz as a provisional SPEC-INST piano default. B3-T2 reports local-model mobility error against f\_split, and V3 confirms or changes the value before G5; SPEC-INST records the confirmed value.

## Interface impact
Item 1 is a documentation-only ICR (comment in `types.hpp`). Items 2 and 3 need a compatible ICR (new members at the end of `PlateParams` with default initialisers: `outline`, `thicknessGrid` and its dimensions, `bridges`, `rimRotationalStiffness`; `rimStiffness` re-documented as k\_rim). Removing or renaming `lengthX`, `lengthY` or `thickness` would be breaking; keep them as the bounding box and the uniform fallback.

## Recommended resolution
Accept items 1 to 3. Apply the ICR before B1 starts, because B1-R1 mirrors these structs in the instrument schema; B2 then implements the full geometry.

## Resolution
Open. Needs the orchestrator's acceptance and a human reviewer for items 2 and 3 (physics input and a numerics default).

Affected phases: B1, B2, B3, C1, C2, V3.
