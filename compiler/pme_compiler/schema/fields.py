# SPDX-License-Identifier: MIT
# ruff: noqa: E501
"""Single source of truth for the instrument file format (B1-R1).

The table below mirrors the interfaces-v2 description structs (engine/include/pme/*.hpp) member by
member, in declaration order. ``generate.py`` derives from it the JSON Schema, the Python models
and the C++ reader/writer, so the three cannot drift apart. A member whose header declares a
non-empty default initialiser is optional in the file (``default`` is set); every other member is
required (docs/dev/instrument-format.md, "Format rules").
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

CURRENT_VERSION = 1
FORMAT_NAME = "pme-instrument"
SCHEMA_ID = "urn:pme:schema:instrument:1"

# Closed vocabulary of x-unit annotations (B1-R1).
UNITS = (
    "1", "index", "m", "kg", "kg/m^3", "Pa", "N", "N/m", "N/m^2", "N/m^p", "N/m^alpha",
    "N/m^(alpha+1)", "Hz", "1/s", "s^(-1/2)", "s", "s/m", "s/kg", "kg/s", "m/s",
    "rad", "cent", "bytes",
)  # fmt: skip


class _Marker:
    """A named marker value."""

    def __init__(self, name: str) -> None:
        self.name = name

    def __repr__(self) -> str:
        return self.name


# The member has no default: it is required in the file.
REQUIRED = _Marker("REQUIRED")
# The member is an all-default struct and may be omitted (it takes the header's defaults).
FACTORY = _Marker("FACTORY")

# Enumerations: C++ type -> enumerators in declaration order (the file uses the names).
ENUMS: dict[str, tuple[str, ...]] = {
    "Boundary": ("Pinned", "Clamped"),
    "BridgeEnd": ("AtL", "At0"),
    "FeltLaw": ("HuntCrossley", "Stulov"),
    "DamperMode": ("Diagonal", "RankOne"),
    "BarrierKind": ("Custom", "Fret", "Fretboard", "Jawari", "Thread", "Sawari"),
    "BodySource": ("PlateModel", "ModalTable", "MeasuredMobilityFit"),
}


@dataclass(frozen=True)
class Field:
    """One struct member.

    ``kind`` is one of: num, int, bool, str, enum, strenum, numarray, intarray, obj, objarray,
    optobj. ``lo``/``hi`` bound a number (or each element of an array); ``lo_ex``/``hi_ex`` make
    the bound exclusive.
    """

    name: str
    kind: str
    unit: str = ""
    ref: str = ""
    desc: str = ""
    spec: str = ""
    default: object = REQUIRED
    lo: float | None = None
    hi: float | None = None
    lo_ex: bool = False
    hi_ex: bool = False
    min_items: int = 0
    one_of: tuple[int, ...] = ()
    values: tuple[str, ...] = ()
    pattern: str = ""


@dataclass(frozen=True)
class Struct:
    """One description struct (``cpp`` False: schema and Python only)."""

    name: str
    spec: str
    doc: str
    fields: tuple[Field, ...]
    cpp: bool = True
    extra: dict[str, object] = field(default_factory=dict)


def num(name: str, unit: str, desc: str, spec: str = "", **kw: Any) -> Field:
    """A float64 member."""
    return Field(name, "num", unit=unit, desc=desc, spec=spec, **kw)


def integer(name: str, unit: str, desc: str, spec: str = "", **kw: Any) -> Field:
    """An integer member."""
    return Field(name, "int", unit=unit, desc=desc, spec=spec, **kw)


POS: dict[str, Any] = {"lo": 0.0, "lo_ex": True}  # > 0
NONNEG: dict[str, Any] = {"lo": 0.0}  # >= 0
UNIT: dict[str, Any] = {"lo": 0.0, "hi": 1.0}  # in [0, 1]

STRUCTS: tuple[Struct, ...] = (
    Struct(
        "Point", "SPEC-API",
        "A point in the body frame (types.hpp, SI-0003).",
        (
            num("x", "m", "Case-frame x", "SPEC-CONV"),
            num("y", "m", "Case-frame y", "SPEC-CONV"),
            num("z", "m", "Normal to the plate, towards the strings", "SPEC-CONV"),
        ),
    ),
    Struct(
        "StringParams", "SPEC-STR",
        "Physical description of one string (string.hpp).",
        (
            num("L", "m", "Speaking length L", **POS),
            num("dCore", "m", "Core diameter d_c", "SPEC-STR Wound strings", **POS),
            num("rhoCore", "kg/m^3", "Core density rho_c", **POS),
            num("ECore", "Pa", "Core Young's modulus E_c", **POS),
            num("dOuter", "m", "Outer (winding) diameter d_o; at least dCore", "SPEC-STR Wound strings", **POS),
            num("rhoWind", "kg/m^3", "Winding density rho_w", **POS),
            num("packing", "1", "Winding packing factor eta_w, 0.75 to 0.9 for round wire", lo=0.0, lo_ex=True, hi=1.0),
            num("epsWind", "1", "Winding stiffness fraction eps_w of the core EI", **NONNEG),
            num("f1Target", "Hz", "Target first-partial frequency f_1 (replaced by the tuning curve when key >= 0)", "SPEC-STR Tension for pitch, SI-0004", **POS),
            num("b1", "1/s", "Intrinsic loss b_1", "SPEC-STR Losses", **NONNEG),
            num("b2", "s^(-1/2)", "Intrinsic loss b_2", "SPEC-STR Losses", **NONNEG),
            num("b3", "s", "Intrinsic loss b_3", "SPEC-STR Losses", **NONNEG),
            Field("sigmaOverride", "numarray", unit="1/s", desc="Optional per-partial sigma_n; empty = use b1..b3", spec="SPEC-STR Losses", lo=0.0),
            Field("boundary", "enum", ref="Boundary", desc="End conditions", spec="SPEC-STR Modes", default="Pinned"),
            num("detuneH", "cent", "Horizontal-polarisation detune Delta_h", "SPEC-STR Polarisations"),
            Field("bridgeEnd", "enum", ref="BridgeEnd", desc="Which end moves with the bridge", spec="SPEC-BRG Decomposition", default="AtL"),
        ),
    ),
    Struct(
        "HammerParams", "SPEC-HAM",
        "Hammer parameters (exciters.hpp).",
        (
            num("mass", "kg", "Hammer mass m_H", **POS),
            num("K", "N/m^p", "Felt stiffness K", "SPEC-HAM Contact", **POS),
            num("p", "1", "Felt exponent p", **POS),
            num("muHC", "s/m", "Hunt-Crossley hysteresis mu_HC", **NONNEG),
            num("width", "m", "Contact window width b_H", **POS),
            num("strikeRatio", "1", "Strike position beta_c = x_c / L", **UNIT),
            Field("law", "enum", ref="FeltLaw", desc="Felt dissipation law", spec="SPEC-HAM Felt dissipation", default="HuntCrossley"),
            num("stulovEps", "1", "Stulov hereditary amplitude epsilon", "SPEC-HAM Alternative: Stulov", **NONNEG),
            num("stulovTau", "s", "Stulov relaxation time tau", "SPEC-HAM Alternative: Stulov", **NONNEG),
            num("vMin", "m/s", "v_min of the velocity map", default=0.3, **NONNEG),
            num("vMax", "m/s", "v_max of the velocity map", default=6.0, **POS),
            num("gammaV", "1", "gamma_v of the velocity map", default=1.5, **POS),
            num("dCheck", "m", "Release distance d_check", default=0.005, **NONNEG),
        ),
    ),
    Struct(
        "UnaCordaParams", "SPEC-HAM",
        "Una corda and soft-pedal parameters (exciters.hpp).",
        (
            num("s0", "1", "Smoothstep lower bound s_0", "SPEC-HAM Una corda (grand)", default=0.3, **UNIT),
            num("s1", "1", "Smoothstep upper bound s_1", "SPEC-HAM Una corda (grand)", default=0.9, **UNIT),
            num("kappaUC", "1", "Stiffness factor kappa_uc", "SPEC-HAM Una corda (grand)", default=0.6, **NONNEG),
            num("deltaPUC", "1", "Exponent shift Delta p_uc", "SPEC-HAM Una corda (grand)", default=0.0),
            num("rhoUpright", "1", "Upright soft-pedal velocity scale rho_up", "SPEC-HAM Soft pedal (upright)", default=0.3, **UNIT),
            integer("leavingString", "index", "Zero-based index of the string that leaves the hammer; -1 for none", "SPEC-HAM String that leaves the hammer", default=0, lo=-1.0),
        ),
    ),
    Struct(
        "PluckPulse", "SPEC-PLK",
        "Generic force-pulse pluck (exciters.hpp).",
        (
            num("duration", "s", "Pulse duration t_p", **POS),
            num("peakForceMax", "N", "Peak force F_p,max at strength 1", "SPEC-PLK Strength map", **POS),
            num("position", "m", "Window centre x_p on the segment's x axis", **NONNEG),
            num("width", "m", "Window width b_p", **POS),
            num("angle", "rad", "Polarisation angle theta_p"),
            num("strengthGamma", "1", "gamma_p of the strength map", "SPEC-PLK Strength map", default=1.0, **POS),
        ),
    ),
    Struct(
        "Plectrum", "SPEC-PLK",
        "Plectrum: pick, quill, mizrab or bachi (exciters.hpp).",
        (
            num("tipMass", "kg", "Tip mass m_p", **POS),
            num("driverStiffness", "N/m", "Spring K_p between tip and driver", **POS),
            num("driverSpeedMax", "m/s", "Driver speed v_d,max at strength 1", **POS),
            num("slipHeight", "m", "Release threshold h_slip", **NONNEG),
            num("contactK", "N/m^alpha", "Contact stiffness K of the one-sided power law", **POS),
            num("contactAlpha", "1", "Contact exponent alpha", **POS),
            num("position", "m", "Default pluck position x_p on the segment's x axis", **NONNEG),
            num("angle", "rad", "Pluck direction theta_p"),
            num("strengthGamma", "1", "gamma_p of the strength map", "SPEC-PLK Strength map", default=1.0, **POS),
        ),
    ),
    Struct(
        "Finger", "SPEC-PLK",
        "Finger with a wide soft Hunt-Crossley flesh contact (exciters.hpp, SI-0005).",
        (
            num("tipMass", "kg", "Finger tip mass m_f", **POS),
            num("driverStiffness", "N/m", "Driver spring K_f between tip and driver", **POS),
            num("driverSpeedMax", "m/s", "Driver speed v_d,max at strength 1", **POS),
            num("slipHeight", "m", "Release threshold h_slip,f", **NONNEG),
            num("contactK", "N/m^alpha", "Flesh contact stiffness K_c,f", **POS),
            num("contactAlpha", "1", "Contact exponent alpha_f", **POS),
            num("muFlesh", "s/m", "Hunt-Crossley flesh damping mu_f", **NONNEG),
            num("width", "m", "Raised-cosine contact width b_f", **POS),
            num("position", "m", "Default pluck position x_p on the segment's x axis", **NONNEG),
            num("angle", "rad", "Pluck direction theta_p"),
            num("strengthGamma", "1", "gamma_p of the strength map", "SPEC-PLK Strength map", default=1.0, **POS),
        ),
    ),
    Struct(
        "InitialDisplacement", "SPEC-PLK",
        "Initial displacement exciter (exciters.hpp).",
        (
            num("amplitudeMax", "m", "Triangle height A_max at strength 1", **POS),
            num("position", "m", "Default apex position x_p on the segment's x axis", **NONNEG),
            num("angle", "rad", "Polarisation angle theta_p"),
            num("strengthGamma", "1", "gamma_p of the strength map", "SPEC-PLK Strength map", default=1.0, **POS),
        ),
    ),
    Struct(
        "ExciterParams", "SPEC-API",
        "An exciter: an object with exactly one key naming the alternative.",
        (
            Field("hammer", "obj", ref="HammerParams", desc="Hammer", spec="SPEC-HAM"),
            Field("pluckPulse", "obj", ref="PluckPulse", desc="Force pulse", spec="SPEC-PLK"),
            Field("plectrum", "obj", ref="Plectrum", desc="Plectrum", spec="SPEC-PLK"),
            Field("finger", "obj", ref="Finger", desc="Finger", spec="SPEC-PLK"),
            Field("initialDisplacement", "obj", ref="InitialDisplacement", desc="Initial displacement", spec="SPEC-PLK"),
        ),
        extra={"variant": True},
    ),
    Struct(
        "DamperParams", "SPEC-DMP",
        "Damper parameters (dampers.hpp).",
        (
            num("xRatio", "1", "Pad centre x_d / L", "SPEC-DMP Damper model", **UNIT),
            num("width", "m", "Pad width b_d", **POS),
            num("Kd", "N/m", "Pad stiffness K_d", **POS),
            num("Rd", "kg/s", "Pad resistance R_d", **NONNEG),
            num("tau0", "s", "Landing time constant tau_0", "SPEC-DMP Timing", default=0.012, **POS),
            num("kRelease", "1", "Release-velocity factor k_r", "SPEC-DMP Timing", default=2.0, **POS),
            num("tauUp", "s", "Lift time constant tau_up", "SPEC-DMP Timing", default=0.004, **POS),
            num("etaD", "1", "Contact shape exponent eta_d", "SPEC-DMP Lift fraction", default=2.0, **POS),
            Field("hasDamper", "bool", desc="False for the undamped top keys", spec="SPEC-DMP Undamped notes", default=True),
            Field("mode", "enum", ref="DamperMode", desc="Diagonal or rank-one", spec="SPEC-DMP Damper model", default="Diagonal"),
        ),
    ),
    Struct(
        "BarrierParams", "SPEC-PLK",
        "Distributed barrier (barriers.hpp).",
        (
            Field("kind", "enum", ref="BarrierKind", desc="Geometry family", spec="SPEC-PLK Barrier geometries", default="Custom"),
            Field("x", "numarray", unit="m", desc="Sample positions x_j along the string", spec="SPEC-PLK Distributed barriers"),
            Field("b", "numarray", unit="m", desc="Profile b(x_j)", spec="SPEC-PLK Distributed barriers"),
            Field("w", "numarray", unit="m", desc="Quadrature weights Delta x_j", spec="SPEC-PLK Distributed barriers"),
            Field("side", "int", unit="1", desc="Side s_b, +1 or -1", spec="SPEC-PLK Distributed barriers", default=1, one_of=(-1, 1)),
            num("K", "N/m^(alpha+1)", "Contact stiffness K_B", "SPEC-PLK Distributed barriers", **NONNEG),
            num("alpha", "1", "Contact exponent alpha", "SPEC-PLK Distributed barriers", **POS),
            num("muB", "s/m", "Contact damping mu_B", "SPEC-PLK Distributed barriers", **NONNEG),
        ),
    ),
    Struct(
        "SegmentDesc", "SPEC-INST",
        "One string segment of a note group (instrument.hpp).",
        (
            Field("string", "obj", ref="StringParams", desc="The string", spec="SPEC-STR"),
            integer("bridgePoint", "index", "Index into instrument.bridgePoints", "SPEC-BRG", lo=0.0),
            num("polarisationAngle", "rad", "Angle of the vertical polarisation axis", "SPEC-BRG"),
            Field("barriers", "objarray", ref="BarrierParams", desc="Barriers this segment collides with", spec="SPEC-PLK"),
            Field("damper", "optobj", ref="DamperParams", desc="Damper, if any", spec="SPEC-DMP", default=None),
            num("muteRatio", "1", "Rear-segment mute m_d", "SPEC-STR Duplex and rear segments", **UNIT),
            Field("tensionModulation", "bool", desc="Enable Kirchhoff-Carrier tension modulation", spec="SPEC-NL"),
            integer("longitudinalModes", "index", "Longitudinal modes M_L", "SPEC-INST Piano defaults", default=8, lo=0.0),
            num("kappaL", "1", "Longitudinal transmission kappa_L", "SPEC-GEO", default=0.026, **NONNEG),
            num("kappaT", "1", "Tension-modulation transmission kappa_T", "SPEC-GEO", **NONNEG),
            num("unisonDetuneCents", "cent", "Unison detune d_s added to the tuning curve", "SPEC-STR Tension for pitch, SI-0004"),
        ),
    ),
    Struct(
        "NoteGroupDesc", "SPEC-API",
        "One key's strings, exciter and una corda map (instrument.hpp).",
        (
            integer("key", "index", "MIDI note number 0..127 (A4 = 69), or -1", "SPEC-API Event addressing, SI-0004", lo=-1.0, hi=127.0),
            Field("segments", "objarray", ref="SegmentDesc", desc="Speaking, rear and duplex segments", spec="SPEC-STR"),
            Field("exciter", "optobj", ref="ExciterParams", desc="Exciter; absent for sympathetic-only groups", spec="SPEC-PLK", default=None),
            Field("unaCorda", "obj", ref="UnaCordaParams", desc="Una corda and soft-pedal map", spec="SPEC-HAM", default=FACTORY),
        ),
    ),
    Struct(
        "BridgePointDesc", "SPEC-BRG",
        "A bridge point (bridge.hpp).",
        (
            Field("position", "obj", ref="Point", desc="Location on the body", spec="SPEC-BODY"),
            num("thetaB", "rad", "Bridge frame rotation theta_b", "SPEC-BRG"),
            num("conductanceH", "s/kg", "Horizontal-axis conductance G_h", "SPEC-BRG Polarisation coupling", **NONNEG),
        ),
    ),
    Struct(
        "RibDesc", "SPEC-BODY",
        "A rib: a straight beam attached to the plate (body.hpp).",
        (
            Field("start", "obj", ref="Point", desc="Start of the beam axis", spec="SPEC-BODY Plate model"),
            Field("end", "obj", ref="Point", desc="End of the beam axis", spec="SPEC-BODY Plate model"),
            num("width", "m", "Beam width", "SPEC-BODY Plate model", **POS),
            num("height", "m", "Beam height", "SPEC-BODY Plate model", **POS),
            num("youngs", "Pa", "Young's modulus along the beam", "SPEC-BODY Plate model", **POS),
            num("density", "kg/m^3", "Density", "SPEC-BODY Plate model", **POS),
        ),
    ),
    Struct(
        "BridgeBeamDesc", "SPEC-BODY",
        "A bridge attached to the plate: a polyline beam (body.hpp).",
        (
            Field("path", "objarray", ref="Point", desc="Polyline vertices in the body frame, z = 0, at least 2", spec="SPEC-BODY Plate model", min_items=2),
            num("width", "m", "Section width", "SPEC-BODY Plate model", **POS),
            num("height", "m", "Section height", "SPEC-BODY Plate model", **POS),
            num("youngs", "Pa", "Young's modulus along the bridge", "SPEC-BODY Plate model", **POS),
            num("density", "kg/m^3", "Density", "SPEC-BODY Plate model", **POS),
        ),
    ),
    Struct(
        "PlateParams", "SPEC-BODY",
        "Plate geometry and wood data (body.hpp, SI-0003).",
        (
            num("lengthX", "m", "Extent along +x; used when outline is empty", "SPEC-BODY Plate model", **NONNEG),
            num("lengthY", "m", "Extent along +y; used when outline is empty", "SPEC-BODY Plate model", **NONNEG),
            num("thickness", "m", "Uniform thickness h, used when thicknessGrid is empty", "SPEC-BODY Plate model", **NONNEG),
            num("grainAngle", "rad", "Grain angle theta_g", "SPEC-BODY Plate model"),
            num("youngsL", "Pa", "E_L along the grain at reference moisture", "SPEC-BODY Plate model", **NONNEG),
            num("youngsR", "Pa", "E_R across the grain at reference moisture", "SPEC-BODY Plate model", **NONNEG),
            num("shearLR", "Pa", "G_LR at reference moisture", "SPEC-BODY Plate model", **NONNEG),
            num("poissonLR", "1", "nu_LR", "SPEC-BODY Plate model"),
            num("poissonRL", "1", "nu_RL", "SPEC-BODY Plate model"),
            num("density", "kg/m^3", "Density rho at reference moisture", "SPEC-BODY Plate model", **NONNEG),
            num("moisture", "1", "Moisture content MC (fraction)", "SPEC-BODY Plate model", **NONNEG),
            num("dEdMcL", "Pa", "Linear coefficient of E_L with MC (Pa per unit MC)", "SPEC-BODY Plate model"),
            num("dEdMcR", "Pa", "Linear coefficient of E_R with MC (Pa per unit MC)", "SPEC-BODY Plate model"),
            num("dGdMc", "Pa", "Linear coefficient of G_LR with MC (Pa per unit MC)", "SPEC-BODY Plate model"),
            num("dRhodMc", "kg/m^3", "Linear coefficient of density with MC (per unit MC)", "SPEC-BODY Plate model"),
            num("lossFactor", "1", "Wood loss factor eta", "SPEC-BODY Modal data", **NONNEG),
            num("rimStiffness", "N/m^2", "Translational rim support stiffness per unit rim length k_rim", "SPEC-BODY Plate model", **NONNEG),
            num("fMax", "Hz", "Highest kept body mode frequency f_max", "SPEC-BODY Plate model", **NONNEG),
            Field("ribs", "objarray", ref="RibDesc", desc="Ribs as straight attached beams", spec="SPEC-BODY Plate model"),
            Field("outline", "objarray", ref="Point", desc="Outline polygon in the body frame, z = 0, not closed; empty = the lengthX x lengthY rectangle", spec="SPEC-BODY Plate model, SI-0003"),
            integer("thicknessNx", "index", "Thickness grid points along x (at least 2 when the grid is used)", "SPEC-BODY Plate model, SI-0003", lo=0.0),
            integer("thicknessNy", "index", "Thickness grid points along y (at least 2 when the grid is used)", "SPEC-BODY Plate model, SI-0003", lo=0.0),
            Field("thicknessGrid", "numarray", unit="m", desc="Thickness h on a regular Nx x Ny grid, row-major [j * Nx + i]; empty = uniform thickness", spec="SPEC-BODY Plate model, SI-0003", **POS),
            Field("bridges", "objarray", ref="BridgeBeamDesc", desc="Bridges as polyline beams", spec="SPEC-BODY Plate model"),
            num("rimRotationalStiffness", "N", "Rotational rim stiffness per unit length k_rim,theta (N m/rad per m); 0 = none", "SPEC-BODY Plate model", **NONNEG),
        ),
    ),
    Struct(
        "BodyParams", "SPEC-BODY",
        "Description of a body (body.hpp).",
        (
            Field("source", "enum", ref="BodySource", desc="Where the modal data comes from", spec="SPEC-BODY", default="PlateModel"),
            Field("plate", "obj", ref="PlateParams", desc="Used when source is PlateModel", spec="SPEC-BODY Plate model"),
            Field("modalTableHash", "str", desc="Manifest hash 'sha256:<64 hex>' of the modal blob when source is not PlateModel; empty otherwise", spec="SPEC-INST Instrument file", pattern="^(sha256:[0-9a-f]{64})?$"),
        ),
    ),
    Struct(
        "ChannelDesc", "SPEC-BODY",
        "One radiation output channel (body.hpp).",
        (
            Field("name", "str", desc="Human-readable channel name", spec="SPEC-BODY Radiation weights"),
            Field("position", "obj", ref="Point", desc="Listening position R_c", spec="SPEC-BODY Radiation weights"),
        ),
    ),
    Struct(
        "TuningCurve", "SPEC-STR",
        "Pitch map (instrument.hpp, SI-0004).",
        (
            num("referencePitch", "Hz", "Frequency of MIDI key 69 (A4)", "SPEC-STR Tension for pitch", default=440.0, **POS),
            Field("stretchCents", "numarray", unit="cent", desc="Per-key stretch indexed by MIDI key, key 0 first; keys beyond the array have 0", spec="SPEC-STR Tension for pitch"),
        ),
    ),
    Struct(
        "Defaults", "SPEC-INST",
        "Instrument-wide default values (instrument.hpp).",
        (
            num("halfPedalP0", "1", "Half-pedal region start P_0", "SPEC-DMP Lift fraction", default=0.35, **UNIT),
            num("halfPedalP1", "1", "Half-pedal region end P_1", "SPEC-DMP Lift fraction", default=0.65, **UNIT),
            num("sympatheticGain", "1", "kappa_s", "SPEC-BODY Sympathetic path", default=1.0, **UNIT),
            num("sympatheticLowPass", "Hz", "f_symp", "SPEC-BODY Sympathetic path", default=5000.0, **POS),
            integer("localBridgeModes", "index", "M_b", "SPEC-INST Piano defaults", default=64, lo=0.0),
            num("localBridgeFSplit", "Hz", "f_split for buildLocalBridge", "SPEC-INST Piano defaults, SI-0003", default=2000.0, **POS),
        ),
    ),
    Struct(
        "InstrumentDesc", "SPEC-API",
        "Complete instrument description (instrument.hpp).",
        (
            Field("name", "str", desc="Instrument name (no trademarks)", spec="SPEC-INST"),
            Field("groups", "objarray", ref="NoteGroupDesc", desc="Note groups", spec="SPEC-INST"),
            Field("bridgePoints", "objarray", ref="BridgePointDesc", desc="Bridge points", spec="SPEC-BRG"),
            Field("body", "obj", ref="BodyParams", desc="Body", spec="SPEC-BODY"),
            Field("channels", "objarray", ref="ChannelDesc", desc="Radiation output channels", spec="SPEC-BODY"),
            Field("tuning", "obj", ref="TuningCurve", desc="Pitch map", spec="SPEC-STR"),
            Field("defaults", "obj", ref="Defaults", desc="Instrument-wide defaults", spec="SPEC-INST Piano defaults", default=FACTORY),
        ),
    ),
    # --- manifest (B1-R3): schema and Python only; the C++ loader reads these by hand -------------
    Struct(
        "BlobProducer", "SPEC-INST",
        "Tool that produced a blob (B1-R3).",
        (
            Field("tool", "str", desc="Producing tool", spec="SPEC-INST Instrument file"),
            Field("version", "str", desc="Producing tool version", spec="SPEC-INST Instrument file"),
        ),
        cpp=False,
    ),
    Struct(
        "BlobArray", "SPEC-INST",
        "One array inside a blob file (B1-R3).",
        (
            Field("name", "str", desc="Array name", spec="SPEC-INST Instrument file"),
            Field("dtype", "strenum", desc="Element type", spec="SPEC-INST Instrument file", values=("float32", "float64", "int32", "uint32")),
            Field("shape", "intarray", unit="index", desc="Shape; positive integers", spec="SPEC-INST Instrument file", lo=1.0, min_items=1),
            Field("unit", "str", desc="Physical unit of the elements (same vocabulary as x-unit)", spec="SPEC-INST Instrument file"),
            integer("offset", "bytes", "Offset in bytes from the start of the file, aligned to the dtype size", "SPEC-INST Instrument file", lo=0.0),
        ),
        cpp=False,
    ),
    Struct(
        "BlobEntry", "SPEC-INST",
        "One blob file of the manifest (B1-R3).",
        (
            Field("hash", "str", desc="'sha256:<64 hex>' of the whole blob file", spec="SPEC-INST Instrument file", pattern="^sha256:[0-9a-f]{64}$"),
            Field("path", "str", desc="Path relative to the instrument file's directory, forward slashes, no '..'", spec="SPEC-INST Instrument file"),
            Field("producer", "obj", ref="BlobProducer", desc="Producing tool and version", spec="SPEC-INST Instrument file"),
            Field("arrays", "objarray", ref="BlobArray", desc="Arrays stored in the file", spec="SPEC-INST Instrument file"),
        ),
        cpp=False,
    ),
)  # fmt: skip

BY_NAME: dict[str, Struct] = {s.name: s for s in STRUCTS}

# Keys of the exciter alternatives and the structs they hold, in declaration order.
EXCITER_ALTERNATIVES: tuple[tuple[str, str], ...] = tuple(
    (f.name, f.ref) for f in BY_NAME["ExciterParams"].fields
)

# Byte size of each blob element type.
DTYPE_SIZE: dict[str, int] = {"float32": 4, "float64": 8, "int32": 4, "uint32": 4}

# Struct members the segment-order, tuning and plate rules of docs/dev/instrument-format.md name.
MAX_ERRORS = 100
MAX_DEPTH = 64
