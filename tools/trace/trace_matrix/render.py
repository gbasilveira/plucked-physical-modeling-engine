# SPDX-License-Identifier: MIT
"""Output of the matrix: self-contained HTML (no scripts, no external assets), a Markdown summary
for ``$GITHUB_STEP_SUMMARY`` and the JSON document (V1-R2, V1-R4)."""

from __future__ import annotations

import json
from html import escape
from typing import Any

STATUS_LABEL = {
    "pass": "pass",
    "fail": "FAIL",
    "missing": "MISSING",
    "not_run_all_systems": "not run on all systems",
    "uncovered": "UNCOVERED",
}

_CSS = """
:root { color-scheme: light dark; --bg:#ffffff; --fg:#1b1f24; --muted:#59636e; --line:#d0d7de;
  --ok:#1a7f37; --bad:#cf222e; --warn:#9a6700; --soft:#f6f8fa; }
@media (prefers-color-scheme: dark) { :root { --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1;
  --line:#30363d; --ok:#3fb950; --bad:#f85149; --warn:#d29922; --soft:#161b22; } }
body { background:var(--bg); color:var(--fg); font:15px/1.45 system-ui, sans-serif; margin:0;
  padding:16px; max-width:1200px; margin-inline:auto; }
h1 { font-size:1.5rem; } h2 { font-size:1.2rem; margin-top:2rem; } h3 { font-size:1rem; }
table { border-collapse:collapse; width:100%; margin:.5rem 0 1rem; }
th, td { border:1px solid var(--line); padding:4px 8px; text-align:left; vertical-align:top; }
th { background:var(--soft); }
.wrap { overflow-x:auto; }
.pass { color:var(--ok); } .fail, .missing, .uncovered { color:var(--bad); font-weight:600; }
.not_run_all_systems { color:var(--warn); font-weight:600; }
.muted { color:var(--muted); } code { font-family:ui-monospace, monospace; font-size:.9em; }
details { margin:.25rem 0; } summary { cursor:pointer; }
.phase { border-top:1px solid var(--line); padding-top:.5rem; }
"""


def _label(status: str) -> str:
    return STATUS_LABEL.get(status, status)


def _chip(status: str, unverified: bool = False) -> str:
    extra = " (unverified)" if unverified else ""
    return f'<span class="{escape(status)}">{escape(_label(status))}{extra}</span>'


def _evidence(item: dict[str, Any]) -> str:
    if not item["evidence"]:
        return '<span class="muted">none</span>'
    lines = []
    for e in item["evidence"]:
        outcomes = ", ".join(f"{n} {k}" for k, n in sorted(e["outcomes"].items())) or "no result"
        systems = ", ".join(e["systems"]) or "-"
        lines.append(
            f'<li><code>{escape(e["name"])}</code> <span class="muted">{escape(e["where"])};'
            f" {escape(outcomes)}; passed on: {escape(systems)}</span></li>"
        )
    return "<ul>" + "".join(lines) + "</ul>"


def _item_rows(items: list[dict[str, Any]], text_key: str) -> str:
    rows = []
    for item in items:
        state = item.get("state", item["status"])
        jobs = ", ".join(item["jobs"]) or "-"
        rows.append(
            "<tr>"
            f"<td><code>{escape(item['id'])}</code></td>"
            f"<td>{escape(item.get(text_key, ''))}</td>"
            f"<td>{_chip(state, item['unverified'])}</td>"
            f"<td><details><summary>{len(item['evidence'])} carrier(s); "
            f"jobs: {escape(jobs)}</summary>"
            f"{_evidence(item)}</details></td>"
            "</tr>"
        )
    return "".join(rows)


def render_html(matrix: dict[str, Any]) -> str:
    """Render ``traceability.html``."""
    t = matrix["totals"]
    out: list[str] = [
        "<!doctype html>",
        '<html lang="en"><head><meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        "<title>Traceability matrix</title>",
        f"<style>{_CSS}</style></head><body>",
        "<h1>Traceability matrix</h1>",
        f"<p>Required systems: {escape(', '.join(matrix['required_systems']))}. "
        f"{t['phases_complete']} of {t['phases']} phases complete. "
        f"<strong>{t['gaps']}</strong> gaps ({t['gaps_known']} on the allow-list, "
        f"<strong>{t['gaps_enforced']}</strong> enforced).",
    ]
    if not matrix["job_results_supplied"]:
        out.append(
            ' <span class="muted">No job results were supplied: steps annotated '
            "<code>Covers:</code> are shown as unverified.</span>"
        )
    out.append("</p>")

    out.append('<h2 id="phases">Phases</h2><div class="wrap"><table>')
    out.append(
        "<tr><th>Phase</th><th>Status</th><th>Requirements covered</th><th>Tests pass</th>"
        "<th>Fail / missing / partial</th><th>Oracles pass</th><th>Gaps</th></tr>"
    )
    for p in matrix["phases"]:
        s = p["summary"]
        out.append(
            f'<tr><td><a href="#phase-{escape(p["id"])}"><code>{escape(p["id"])}</code></a> '
            f"{escape(p['name'])}</td><td>{escape(p['status'])}</td>"
            f"<td>{s['requirements_covered']} / {s['requirements']}</td>"
            f"<td>{s['tests_pass']} / {s['tests']}</td>"
            f"<td>{s['tests_fail']} / {s['tests_missing']} / {s['tests_not_run_all_systems']}</td>"
            f"<td>{s['oracles_pass']} / {s['oracles']}</td>"
            f"<td>{s['gaps']} ({s['gaps_enforced']} enforced)</td></tr>"
        )
    out.append("</table></div>")

    out.append('<h2 id="gates">Gates</h2><div class="wrap"><table>')
    out.append(
        "<tr><th>Gate</th><th>Status</th><th>Required phases not complete</th>"
        "<th>Gaps in required phases</th><th>Oracle rows still missing</th></tr>"
    )
    for g in matrix["gates"]:
        none_required = "-" if g["oracles"] else "none required"
        out.append(
            f"<tr><td><code>{escape(g['id'])}</code> {escape(g['name'])}</td>"
            f"<td>{escape(g['status'])}</td>"
            f"<td>{escape(', '.join(g['phases_not_complete']) or '-')}</td>"
            f"<td>{g['phase_gaps']}</td>"
            f"<td>{escape(', '.join(g['oracles_missing']) or none_required)}</td></tr>"
        )
    out.append("</table></div>")

    out.append('<h2 id="oracles">Oracle rows</h2><div class="wrap"><table>')
    out.append(
        "<tr><th>ID</th><th>Oracle</th><th>Owner</th><th>Gate</th><th>Registry</th><th>CI</th>"
        "<th>Inconsistencies</th></tr>"
    )
    for o in matrix["oracles"]:
        problems = "; ".join(o["inconsistencies"]) or "-"
        out.append(
            f"<tr><td><code>{escape(o['id'])}</code></td><td>{escape(o['name'])}</td>"
            f"<td>{escape(', '.join(o['owning_phases']))}</td><td>{escape(o['gate'])}</td>"
            f"<td>{escape(o['registry_status'])}</td>"
            f"<td>{_chip(o['ci']['status'], o['ci']['unverified'])}</td>"
            f"<td>{escape(problems)}</td></tr>"
        )
    out.append("</table></div>")

    out.append('<h2 id="gaps">Gaps</h2>')
    if matrix["gaps"]:
        out.append('<div class="wrap"><table><tr><th>ID</th><th>Phase</th><th>Kind</th>')
        out.append("<th>Detail</th><th>State</th></tr>")
        for g in matrix["gaps"]:
            state = "allow-listed" if g["known"] else ("ENFORCED" if g["enforced"] else "open")
            out.append(
                f"<tr><td><code>{escape(g['id'])}</code></td><td>{escape(g['phase'])}</td>"
                f"<td>{escape(g['kind'])}</td><td>{escape(g['detail'])}</td><td>{state}</td></tr>"
            )
        out.append("</table></div>")
    else:
        out.append("<p>No gaps.</p>")
    if matrix["stale_known_gaps"]:
        out.append("<h3>Allow-list entries that match no gap (remove them)</h3><ul>")
        for k in matrix["stale_known_gaps"]:
            out.append(f"<li><code>{escape(k['id'])}</code> ({escape(k['phase'])})</li>")
        out.append("</ul>")
    if matrix["unknown_ids"]:
        out.append(
            "<p>IDs carried by tests but absent from the plan: "
            + ", ".join(f"<code>{escape(i)}</code>" for i in matrix["unknown_ids"])
            + ".</p>"
        )

    out.append("<h2>Per phase</h2>")
    for p in matrix["phases"]:
        out.append(
            f'<section class="phase" id="phase-{escape(p["id"])}">'
            f"<h3><code>{escape(p['id'])}</code> {escape(p['name'])} "
            f'<span class="muted">({escape(p["status"])})</span></h3>'
        )
        out.append('<div class="wrap"><table><tr><th>ID</th><th>Text</th><th>State</th>')
        out.append("<th>Carried by</th></tr>")
        out.append(_item_rows(p["requirements"], "text"))
        out.append(_item_rows(p["tests"], "criterion"))
        out.append(_item_rows(p["oracles"], "text"))
        out.append("</table></div></section>")
    out.append("</body></html>")
    return "\n".join(out) + "\n"


def render_markdown(matrix: dict[str, Any]) -> str:
    """Render the per-phase summary for ``$GITHUB_STEP_SUMMARY``."""
    t = matrix["totals"]
    lines = [
        "### Traceability",
        "",
        f"{t['phases_complete']} of {t['phases']} phases complete; **{t['gaps']}** gaps "
        f"({t['gaps_known']} allow-listed, **{t['gaps_enforced']}** enforced).",
        "",
        "| Phase | Status | Requirements | Tests | Oracles | Gaps |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    hidden = 0
    for p in matrix["phases"]:
        s = p["summary"]
        if p["status"] == "not started" and s["gaps_enforced"] == 0:
            hidden += 1
            continue
        lines.append(
            f"| {p['id']} | {p['status']} | {s['requirements_covered']}/{s['requirements']} "
            f"| {s['tests_pass']}/{s['tests']} | {s['oracles_pass']}/{s['oracles']} "
            f"| {s['gaps']} ({s['gaps_enforced']} enforced) |"
        )
    if hidden:
        lines += ["", f"{hidden} phases not started are not listed (see traceability.html)."]
    lines += [
        "",
        "| Gate | Status | Phases not complete | Phase gaps | Oracle rows missing |",
        "| --- | --- | --- | ---: | --- |",
    ]
    for g in matrix["gates"]:
        missing = ", ".join(g["oracles_missing"]) or ("-" if g["oracles"] else "none required")
        lines.append(
            f"| {g['id']} {g['name']} | {g['status']} "
            f"| {', '.join(g['phases_not_complete']) or '-'} | {g['phase_gaps']} | {missing} |"
        )
    enforced = [g for g in matrix["gaps"] if g["enforced"]]
    if enforced:
        lines += ["", "Enforced gaps (these fail the `traceability` job):", ""]
        lines += [f"- `{g['id']}` ({g['phase']}, {g['kind']}): {g['detail']}" for g in enforced]
    stale = matrix["stale_known_gaps"]
    if stale:
        lines += [
            "",
            "Allow-list entries that match no gap: " + ", ".join(f"`{k['id']}`" for k in stale),
        ]
    lines.append("")
    return "\n".join(lines)


def render_json(matrix: dict[str, Any]) -> str:
    """Render ``traceability.json``."""
    return json.dumps(matrix, indent=2, sort_keys=False) + "\n"
