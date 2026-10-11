# SPDX-License-Identifier: MIT
"""Branch protection for ``main`` from ``required_checks.json`` (P0.2-R7). Standard library only.

    python3 tools/ci/apply_branch_protection.py --repo OWNER/REPO            # dry run (default)
    GITHUB_TOKEN=... python3 tools/ci/apply_branch_protection.py --repo OWNER/REPO --apply

The dry run prints the REST request that ``--apply`` would send. Applying needs a token with
repository administration rights; it is the orchestrator's or the owner's step, never CI's.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

CHECKS_FILE = Path(__file__).resolve().with_name("required_checks.json")
API = "https://api.github.com"


def load_checks(path: Path = CHECKS_FILE) -> list[str]:
    """Return the required status-check names."""
    data = json.loads(path.read_text(encoding="utf-8"))
    checks = data["required_checks"]
    if not isinstance(checks, list) or not all(isinstance(c, str) for c in checks):
        raise ValueError(f"{path}: 'required_checks' must be a list of strings")
    return checks


def build_request(repo: str, branch: str, checks: list[str]) -> tuple[str, str, dict[str, Any]]:
    """Return (method, URL, JSON body) of the "update branch protection" request."""
    body: dict[str, Any] = {
        "required_status_checks": {"strict": True, "contexts": checks},
        "enforce_admins": False,
        "required_pull_request_reviews": {
            "required_approving_review_count": 1,
            "dismiss_stale_reviews": True,
            "require_code_owner_reviews": False,
        },
        "restrictions": None,
        "allow_force_pushes": False,
        "allow_deletions": False,
    }
    return "PUT", f"{API}/repos/{repo}/branches/{branch}/protection", body


def main(argv: list[str] | None = None) -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, help="OWNER/REPO")
    parser.add_argument("--branch", default="main")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="print the request (default)")
    mode.add_argument("--apply", action="store_true", help="send the request")
    args = parser.parse_args(argv)

    method, url, body = build_request(args.repo, args.branch, load_checks())
    if not args.apply:
        print(f"{method} {url}")
        print("Accept: application/vnd.github+json")
        print("Authorization: Bearer <token from GITHUB_TOKEN; not needed for the dry run>")
        print(json.dumps(body, indent=2))
        print("dry run: nothing was sent")
        return 0

    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is not set", file=sys.stderr)
        return 2
    request = urllib.request.Request(
        url,
        data=json.dumps(body).encode(),
        method=method,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(request) as response:
            print(f"HTTP {response.status}: branch protection applied to {args.branch}")
    except urllib.error.HTTPError as exc:
        print(f"HTTP {exc.code}: {exc.read().decode(errors='replace')}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
