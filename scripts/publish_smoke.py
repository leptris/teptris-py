"""The publish smoke (teptris-py#172): the REGISTRY half of the
release gate. The published-artifact smoke installs wheels, so it
stayed green through the wheels-only waves (0.3.30/31/33) — the sdist
gap surfaced only in binding-bench's prev fetch. This smoke reads the
PyPI JSON API and asserts a release carries BOTH halves: wheels AND
the sdist. Run after every publish:

    python3 scripts/publish_smoke.py 0.3.34.0          # one version
    python3 scripts/publish_smoke.py --all             # every release

Exit 0 only when every requested release carries both halves on the
registry; nonzero prints one problem line per failing release. The
checks live in check_release()/check_all() so the unit suite can
exercise them without a network.
"""
from __future__ import annotations

import json
import sys
import urllib.request

PYPI_JSON = "https://pypi.org/pypi/teptris/json"


def check_release(version: str, files: list[dict]) -> list[str]:
    """One release's both-halves check. `files` is the PyPI JSON
    release list (packagetype/yanked entries). Returns problem
    strings; empty = the release carries wheels AND sdist."""
    live = [f for f in files if not f.get("yanked")]
    if not files:
        return [f"{version}: no files on the registry at all"]
    if not live:
        return [f"{version}: all {len(files)} files are yanked"]
    problems: list[str] = []
    if not any(f.get("packagetype") == "bdist_wheel" for f in live):
        problems.append(f"{version}: no live wheel on the registry")
    if not any(f.get("packagetype") == "sdist" for f in live):
        problems.append(f"{version}: WHEELS-ONLY - the sdist is missing "
                        "on the registry (teptris-py#172 class)")
    return problems


def check_all(payload: dict) -> list[str]:
    """Every shipped (non-empty) release in the PyPI payload must
    carry both halves. Returns problem strings, newest first."""
    releases = payload.get("releases", {})
    problems: list[str] = []
    for version in sorted(releases, reverse=True):
        files = releases[version]
        if not files:
            continue  # placeholder/removed entries carry no files
        problems.extend(check_release(version, files))
    return problems


def fetch_json() -> dict:
    with urllib.request.urlopen(PYPI_JSON, timeout=30) as r:
        return json.loads(r.read().decode("utf-8"))


def main(argv: list[str]) -> int:
    args = argv[1:]
    payload: dict | None = None
    if "--json" in args:  # test seam: inline payload instead of PyPI
        i = args.index("--json")
        payload = json.loads(args[i + 1])
        del args[i:i + 2]
    if not args:
        print("usage: publish_smoke.py [--json PAYLOAD] VERSION... | --all",
              file=sys.stderr)
        return 2
    if payload is None:
        payload = fetch_json()
    problems: list[str] = []
    if args == ["--all"]:
        problems = check_all(payload)
    else:
        for version in args:
            files = payload.get("releases", {}).get(version)
            if files is None:
                problems.append(f"{version}: not on the registry")
                continue
            problems.extend(check_release(version, files))
    for p in problems:
        print(f"publish smoke FAIL: {p}")
    if problems:
        return 1
    print("publish smoke OK: every requested release carries "
          "wheels + sdist")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
