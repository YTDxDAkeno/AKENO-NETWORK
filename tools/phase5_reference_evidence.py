#!/usr/bin/env python3
"""Read-only evidence check for the Phase 5 source-research snapshots."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


SNAPSHOTS = {
    "np-fake-signin": "03403bc7bc33e339cc30e7811847c66655af2756",
    "offact": "8c870eb809cc46f96835e977cae3354b4cf70247",
    "ps5-theme": "e29735f852e6d7bbd2140355a8852b72bc9207de",
    "etaHEN": "dafa13b562ddb137a4b4a97b9aaa287c0c57cc9c",
}

EVIDENCE = (
    ("np-fake-signin", "README.md", "by writing NP files and patching registry/ShellCore state", "NP Fake Signin describes local NP-file and registry/ShellCore-state changes."),
    ("np-fake-signin", "np-fake-signin.c", "write_np_files(uint32_t userId)", "NP Fake Signin contains a local NP-file writer."),
    ("np-fake-signin", "np-fake-signin.c", "PS4 only - needed for memory patching", "The shown ShellCore memory-patching path is guarded as PS4-only."),
    ("offact", "README.md", "Does not enable online PSN gaming", "OffAct explicitly does not claim online PSN gaming."),
    ("offact", "offact.c", "OffAct_SetAccountId", "OffAct changes a local account-ID registry value."),
    ("ps5-theme", "docs/how-it-works.md", "Themes are applied as **read-only nullfs overlays**", "ps5-theme documents in-memory, read-only asset overlays."),
    ("ps5-theme", "docs/how-it-works.md", "ShellUI doesn't read `/system_ex` directly", "ps5-theme documents ShellUI sandbox views rather than a direct system path."),
    ("etaHEN", "Source Code/bootstrapper/Byepervisor/hen/src/patch_shellcore.cpp", "don't have offsets for this firmware", "etaHEN rejects firmware without a matching ShellCore offset set."),
)


@dataclass(frozen=True)
class EvidenceResult:
    repository: str
    path: str
    statement: str
    found: bool


class EvidenceError(RuntimeError):
    pass


def scan_sources(root: Path) -> list[EvidenceResult]:
    results: list[EvidenceResult] = []
    for repository, relative_path, marker, statement in EVIDENCE:
        path = root / repository / relative_path
        if not path.is_file():
            raise EvidenceError(f"required source file is missing: {repository}/{relative_path}")
        results.append(EvidenceResult(repository, relative_path, statement, marker in path.read_text(errors="replace")))
    return results


def verify_snapshots(root: Path) -> None:
    for repository, expected_revision in SNAPSHOTS.items():
        try:
            result = subprocess.run(["git", "-C", str(root / repository), "rev-parse", "HEAD"], check=True, capture_output=True, text=True)
        except (OSError, subprocess.CalledProcessError) as error:
            raise EvidenceError(f"could not determine revision for {repository}") from error
        revision = result.stdout.strip()
        if revision != expected_revision:
            raise EvidenceError(f"{repository}: expected {expected_revision[:12]}, found {revision[:12] or 'unknown'}")


def render_report(results: list[EvidenceResult]) -> str:
    lines = ["Phase 5 reference-source evidence", ""]
    for result in results:
        lines.append(f"{'FOUND' if result.found else 'MISSING'}: {result.repository}/{result.path}")
        lines.append(f"  {result.statement}")
    lines.extend(["", "This confirms only the listed source statements.", "It does not establish Game Base compatibility, native party audio, or firmware 12.20 support."])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("references", type=Path, help="directory containing the four local reference checkouts")
    parser.add_argument("--skip-revision-check", action="store_true", help="only for fixture tests")
    args = parser.parse_args(argv)
    try:
        root = args.references.resolve()
        if not args.skip_revision_check:
            verify_snapshots(root)
        results = scan_sources(root)
    except EvidenceError as error:
        print(f"evidence check failed: {error}", file=sys.stderr)
        return 2
    print(render_report(results))
    return 0 if all(result.found for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
