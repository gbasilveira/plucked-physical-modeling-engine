# SPDX-License-Identifier: MIT
"""Readers of the planning documents: phase cards and gates (docs/plan/phasing-plan.md), phase and
gate status (docs/status/phase-status.md) and the oracle registry (engine/tests/oracles.md).

The readers fail loudly (PlanError) on a malformed card instead of skipping it: a card that cannot
be parsed would otherwise hide its requirements from the matrix.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from .ids import ID_PATTERN, PHASE_ID_PATTERN, PHASE_ID_RE


class PlanError(Exception):
    """A planning document does not have the expected structure."""


@dataclass(frozen=True)
class Requirement:
    """A requirement of a phase card, for example ``A2-R1``."""

    id: str
    text: str


@dataclass(frozen=True)
class TestSpec:
    """A test of a phase card (``A2-T1``) and its criterion."""

    __test__ = False  # not a pytest class

    id: str
    criterion: str


@dataclass
class PhaseCard:
    """One ``### <ID> <name>`` card."""

    id: str
    name: str
    requirements: list[Requirement] = field(default_factory=list)
    tests: list[TestSpec] = field(default_factory=list)
    oracle_ids: list[str] = field(default_factory=list)  # ORC rows the Tests line names


@dataclass
class Gate:
    """One row of the Gates table."""

    id: str
    name: str
    requires_phases: list[str]
    requires_gates: list[str]
    oracle_ids: list[str]
    criteria: str


@dataclass(frozen=True)
class OracleRow:
    """One row of the oracle registry."""

    id: str
    name: str
    owning_phases: tuple[str, ...]
    gate: str
    status: str
    location: str


# ---- Phase cards -----------------------------------------------------------------------------

_HEADING_RE = re.compile(rf"^### ({PHASE_ID_PATTERN}) (.+?)\s*$")
_FIELD_RE = re.compile(r"^- \*\*([^*]+?):?\*\*:?\s?(.*)$")
_REQUIREMENT_RE = re.compile(rf"^\s+(\d+)\.\s+({ID_PATTERN})\s+(.*\S)\s*$")
_ORC_SEGMENT_RE = re.compile(r"^(ORC-[0-9]{2})\b")


def _card_blocks(lines: list[str]) -> list[tuple[str, str, list[str]]]:
    """Return (id, name, body lines) of every phase card."""
    blocks: list[tuple[str, str, list[str]]] = []
    current: tuple[str, str, list[str]] | None = None
    in_fence = False
    for line in lines:
        if line.startswith("```"):
            in_fence = not in_fence
        heading = None if in_fence else _HEADING_RE.match(line)
        if heading:
            current = (heading.group(1), heading.group(2), [])
            blocks.append(current)
            continue
        if not in_fence and line.startswith("## "):
            current = None
        elif current is not None:
            current[2].append(line)
    return blocks


def _field_regions(body: list[str]) -> dict[str, list[str]]:
    """Map each ``- **Field:**`` of a card to its text lines (first line without the label)."""
    regions: dict[str, list[str]] = {}
    key: str | None = None
    for line in body:
        match = _FIELD_RE.match(line)
        if match:
            key = match.group(1).strip().rstrip(":")
            regions[key] = [match.group(2)]
        elif key is not None:
            regions[key].append(line)
    return regions


def _parse_requirements(card: PhaseCard, region: list[str]) -> None:
    current: list[str] | None = None
    items: list[list[str]] = []
    for line in region:
        match = _REQUIREMENT_RE.match(line)
        if match:
            current = [match.group(2), match.group(3)]
            items.append(current)
        elif line.strip() and current is not None:
            current[1] += " " + line.strip()
        elif line.strip() and not line.strip().startswith(("-", "|")):
            raise PlanError(f"{card.id}: unparsable line in Requirements: {line.strip()!r}")
    if not items:
        raise PlanError(f"{card.id}: the Requirements field lists no requirement")
    for number, (item_id, text) in enumerate(items, start=1):
        expected = f"{card.id}-R{number}"
        if item_id != expected:
            raise PlanError(f"{card.id}: requirement {number} is {item_id}, expected {expected}")
        card.requirements.append(Requirement(item_id, text))


def _parse_tests(card: PhaseCard, region: list[str]) -> None:
    text = " ".join(part.strip() for part in region if part.strip())
    if not text:
        raise PlanError(f"{card.id}: the Tests field is empty")
    number = 0
    for raw in text.split(";"):
        segment = raw.strip().rstrip(".").strip()
        if not segment:
            continue
        test = re.match(rf"^({re.escape(card.id)}-T(\d+))\s+(.+)$", segment)
        oracle = _ORC_SEGMENT_RE.match(segment)
        if test:
            number += 1
            if int(test.group(2)) != number:
                raise PlanError(
                    f"{card.id}: test {test.group(1)} out of sequence (expected T{number})"
                )
            card.tests.append(TestSpec(test.group(1), test.group(3)))
        elif oracle:
            if oracle.group(1) not in card.oracle_ids:
                card.oracle_ids.append(oracle.group(1))
        elif segment.lower() != "common tests":
            raise PlanError(f"{card.id}: unparsable item in Tests: {segment!r}")


def parse_phase_cards(plan_text: str) -> list[PhaseCard]:
    """Parse every phase card of the phasing plan; raises PlanError on a malformed card."""
    cards: list[PhaseCard] = []
    seen: set[str] = set()
    for card_id, name, body in _card_blocks(plan_text.splitlines()):
        if card_id in seen:
            raise PlanError(f"{card_id}: card appears twice")
        seen.add(card_id)
        card = PhaseCard(card_id, name)
        regions = _field_regions(body)
        if "Requirements" not in regions:
            raise PlanError(f"{card_id}: no Requirements field")
        if "Tests" not in regions:
            raise PlanError(f"{card_id}: no Tests field")
        _parse_requirements(card, regions["Requirements"])
        _parse_tests(card, regions["Tests"])
        cards.append(card)
    if not cards:
        raise PlanError("no phase card found (expected '### <ID> <name>' headings)")
    return cards


# ---- Gates -----------------------------------------------------------------------------------

DASHES = "\u2013-"  # en dash and hyphen: both are used for ranges like A1-A8
_ORC_LIST_RE = re.compile(r"ORC-(\d{2})((?:\s*(?:,|and|to)\s*\d{2}\b)*)")
_ORC_TAIL_RE = re.compile(r"(,|and|to)\s*(\d{2})")


def parse_orc_ids(text: str) -> list[str]:
    """Expand 'ORC-01 to 09, 11 to 13 and 19' into the list of oracle IDs it names."""
    found: list[str] = []

    def add(number: int) -> None:
        item = f"ORC-{number:02d}"
        if item not in found:
            found.append(item)

    for match in _ORC_LIST_RE.finditer(text):
        previous = int(match.group(1))
        add(previous)
        for joiner, digits in _ORC_TAIL_RE.findall(match.group(2)):
            number = int(digits)
            if joiner == "to":
                for between in range(previous + 1, number + 1):
                    add(between)
            else:
                add(number)
            previous = number
    return found


def _expand_phase_list(text: str) -> tuple[list[str], list[str]]:
    """Return (phase IDs, gate IDs) named by a 'Requires complete' cell; ranges A1-A8 expand."""
    phases: list[str] = []
    gates: list[str] = []
    for letter, start, end_letter, end in re.findall(
        rf"\b([A-Z])(\d+)[{DASHES}]([A-Z])(\d+)\b", text
    ):
        if letter == end_letter:
            phases.extend(f"{letter}{n}" for n in range(int(start), int(end) + 1))
    text = re.sub(rf"\b[A-Z]\d+[{DASHES}][A-Z]\d+\b", " ", text)
    for token in re.findall(rf"\b{PHASE_ID_PATTERN}\b", text):
        if re.fullmatch(r"G\d+", token):
            gates.append(token)
        elif token not in phases:
            phases.append(token)
    return phases, gates


def parse_gates(plan_text: str) -> list[Gate]:
    """Parse the Gates table of the phasing plan."""
    lines = plan_text.splitlines()
    try:
        start = next(i for i, line in enumerate(lines) if line.strip() == "## Gates")
    except StopIteration:
        raise PlanError("no '## Gates' section") from None
    gates: list[Gate] = []
    for line in lines[start + 1 :]:
        if line.startswith("## "):
            break
        if not re.match(r"^\| G\d", line):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 4:
            raise PlanError(f"Gates table row has {len(cells)} cells, expected 4: {line!r}")
        head = re.match(r"^(G\d+)\s+(.*)$", cells[0])
        if head is None:
            raise PlanError(f"Gates table row does not start with a gate ID: {line!r}")
        phases, gate_deps = _expand_phase_list(cells[1])
        gates.append(
            Gate(head.group(1), head.group(2), phases, gate_deps, parse_orc_ids(cells[2]), cells[2])
        )
    if not gates:
        raise PlanError("the Gates table has no row")
    return gates


# ---- Status ----------------------------------------------------------------------------------

PHASE_STATUSES = (
    "not started",
    "ready",
    "in progress",
    "in review",
    "complete",
    "blocked",
    "skipped",
)


def parse_phase_status(status_text: str) -> tuple[dict[str, str], dict[str, str]]:
    """Return ({phase ID: status}, {gate ID: 'passed' | 'not passed'}) from phase-status.md."""
    phases: dict[str, str] = {}
    gates: dict[str, str] = {}
    for line in status_text.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 4:
            continue
        if PHASE_ID_RE.fullmatch(cells[0]) and cells[3] in PHASE_STATUSES:
            phases[cells[0]] = cells[3]
        elif re.fullmatch(r"G\d+", cells[0]) and len(cells) >= 3:
            gates[cells[0]] = "passed" if cells[2].lower().startswith("passed") else "not passed"
    if not phases:
        raise PlanError("no phase row found in the status file")
    return phases, gates


# ---- Oracle registry -------------------------------------------------------------------------

ORACLE_STATUSES = ("not implemented", "implemented", "passing")


def parse_oracle_registry(registry_text: str) -> list[OracleRow]:
    """Parse the table of engine/tests/oracles.md."""
    rows: list[OracleRow] = []
    header: list[str] | None = None
    for line in registry_text.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells and cells[0] == "ID":
            header = [cell.lower() for cell in cells]
            continue
        if header is None or not re.fullmatch(r"ORC-[0-9]{2}", cells[0]):
            continue
        if len(cells) != len(header):
            raise PlanError(
                f"{cells[0]}: registry row has {len(cells)} cells, expected {len(header)}"
            )
        row = dict(zip(header, cells, strict=True))
        status = row.get("status", "")
        if status not in ORACLE_STATUSES:
            raise PlanError(f"{cells[0]}: unknown registry status {status!r}")
        rows.append(
            OracleRow(
                id=cells[0],
                name=row.get("oracle", ""),
                owning_phases=tuple(PHASE_ID_RE.findall(row.get("owning phase", ""))),
                gate=row.get("gate", ""),
                status=status,
                location=row.get("test location", ""),
            )
        )
    if not rows:
        raise PlanError("no ORC row found in the oracle registry")
    return rows
