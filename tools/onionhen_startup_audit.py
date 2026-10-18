#!/usr/bin/env python3
"""Offline source audit for onionHEN's pinned startup behavior.

Run this against a local checkout of aydencharles/onionHEN at v0.9.0:

    python3 tools/onionhen_startup_audit.py /path/to/onionHEN

The script does not fetch code, execute a payload, or modify the checkout.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


PINNED_REVISION = "aaa4264588e00da33faccc7642d27c3c4cc339b1"
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inc"}

WRITE_CALL = re.compile(
    r"\b(?:write_file_atomic|fopen|open|creat|mkdir|rename|remove|unlink)\s*\("
)
SYSTEM_PATH = re.compile(r"/system(?:_ex)?(?:/[A-Za-z0-9_.-]+)*")
REMOUNT_CALL = re.compile(
    r'\bremount\s*\(\s*"[^"]+"\s*,\s*"(?P<path>/system(?:_ex)?)"\s*\)'
)
UPDATE_DELETE = re.compile(r'\bunlink\s*\(\s*"(?P<path>/update/PS5UPDATE\.PUP(?:\.net\.temp)?)"\s*\)')
ICON_PATH = re.compile(r"/system(?:_ex)?/[^\"']*(?:icon|onionhen)[^\"']*\.(?:png|jpg|jpeg|svg)")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    detail: str


class AuditError(RuntimeError):
    """Raised when the provided source tree cannot be audited reliably."""


def source_files(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.joinpath("source").rglob("*")
        if path.is_file() and path.suffix in SOURCE_EXTENSIONS
    )


def find_system_writes(root: Path) -> list[Finding]:
    findings: list[Finding] = []
    for path in source_files(root):
        for line_number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if not WRITE_CALL.search(line):
                continue
            for system_path in SYSTEM_PATH.findall(line):
                findings.append(Finding(str(path.relative_to(root)), line_number, system_path))
    return findings


def find_remounts(root: Path) -> list[Finding]:
    path = root / "source/bootstrapper/source/bootstrap_filesystem.c"
    if not path.is_file():
        raise AuditError(f"required source file is missing: {path.relative_to(root)}")

    findings: list[Finding] = []
    for line_number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        match = REMOUNT_CALL.search(line)
        if match:
            findings.append(Finding(str(path.relative_to(root)), line_number, match.group("path")))
    return findings


def find_update_deletions(root: Path) -> list[Finding]:
    path = root / "source/bootstrapper/source/bootstrap_filesystem.c"
    if not path.is_file():
        raise AuditError(f"required source file is missing: {path.relative_to(root)}")

    findings: list[Finding] = []
    for line_number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        match = UPDATE_DELETE.search(line)
        if match:
            findings.append(Finding(str(path.relative_to(root)), line_number, match.group("path")))
    return findings


def find_system_icon_paths(root: Path) -> list[Finding]:
    findings: list[Finding] = []
    for path in source_files(root):
        for line_number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            for icon_path in ICON_PATH.findall(line):
                findings.append(Finding(str(path.relative_to(root)), line_number, icon_path))
    return findings


def audit_source_tree(root: Path) -> dict[str, list[Finding]]:
    if not root.joinpath("source").is_dir():
        raise AuditError(f"source directory is missing: {root / 'source'}")
    return {
        "system-partition writes": find_system_writes(root),
        "system-partition remounts": find_remounts(root),
        "downloaded firmware-update deletions": find_update_deletions(root),
        "hardcoded system-partition icon paths": find_system_icon_paths(root),
    }


def verify_revision(root: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise AuditError("could not determine the source checkout revision") from error

    revision = result.stdout.strip()
    if revision != PINNED_REVISION:
        raise AuditError(
            f"expected onionHEN {PINNED_REVISION[:7]}, found {revision[:12] or 'unknown'}"
        )
    return revision


def render_report(revision: str, results: dict[str, list[Finding]]) -> str:
    lines = [
        "onionHEN startup source audit",
        f"revision: {revision}",
        "",
    ]
    for category, findings in results.items():
        lines.append(f"{category}: {len(findings)}")
        for finding in findings:
            lines.append(f"  - {finding.path}:{finding.line}: {finding.detail}")
        if not findings:
            lines.append("  - none found")
        lines.append("")
    lines.extend(
        [
            "manual review still required:",
            "  - Trace other filesystem writes and first-run paths beyond these signatures.",
            "  - Verify any no-remount design in isolated software builds; this scan cannot prove it starts.",
            "  - Validate Settings and HomeUI icon behavior without a system-partition asset.",
            "  - Assess firmware 12.20, loader/kstuff coexistence, restart behavior, and rollback separately.",
            "",
            "This is a static, offline source report. It is not a console-safety approval.",
        ]
    )
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="local onionHEN checkout to inspect")
    args = parser.parse_args(argv)

    try:
        root = args.source.resolve()
        revision = verify_revision(root)
        results = audit_source_tree(root)
    except AuditError as error:
        print(f"audit failed: {error}", file=sys.stderr)
        return 2

    print(render_report(revision, results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
