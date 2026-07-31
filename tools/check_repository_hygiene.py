"""Fail on private identifiers, secrets, binaries, or large fork-owned files."""

from __future__ import annotations

import argparse
import re
import subprocess
from collections.abc import Iterable
from pathlib import Path

DEFAULT_BASE = "1ad1895d5ff0bbe69c61243f9996aede713d1b5f"
MAX_FILE_BYTES = 1_000_000
FORBIDDEN_SUFFIXES = {
    ".dll",
    ".exe",
    ".f64",
    ".mov",
    ".mp4",
    ".obj",
    ".pdb",
    ".raw",
    ".yuv",
}
FORBIDDEN_PATTERNS = {
    "private project filename": re.compile(rb"card-[0-9]+", re.IGNORECASE),
    "private project name": re.compile(rb"buddy.?check", re.IGNORECASE),
    "personal name": re.compile(rb"matthew\s+penkala", re.IGNORECASE),
    "local Windows path": re.compile(
        rb"\b[a-z]:\\(?:users|codextemp|codextools|pending projects)\\",
        re.IGNORECASE,
    ),
    "GitHub token": re.compile(rb"gh[pousr]_[A-Za-z0-9_]{20,}"),
    "GitHub fine-grained token": re.compile(rb"github_pat_[A-Za-z0-9_]{20,}"),
    "AWS access key": re.compile(rb"AKIA[0-9A-Z]{16}"),
    "Slack token": re.compile(rb"xox[baprs]-[A-Za-z0-9-]{20,}"),
    "private key": re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
}


def git_lines(root: Path, *args: str) -> list[str]:
    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return [line for line in completed.stdout.splitlines() if line]


def candidate_paths(root: Path, base: str) -> list[Path]:
    paths = set(
        git_lines(
            root,
            "diff",
            "--name-only",
            "--diff-filter=ACMR",
            base,
            "--",
        )
    )
    paths.update(git_lines(root, "ls-files", "--others", "--exclude-standard"))
    return [root / path for path in sorted(paths)]


def scan_payload(
    findings: list[str],
    label: str,
    suffix: str,
    payload: bytes,
) -> None:
    if suffix.lower() in FORBIDDEN_SUFFIXES:
        findings.append(f"{label}: forbidden generated/binary suffix")
    if len(payload) > MAX_FILE_BYTES:
        findings.append(
            f"{label}: {len(payload)} bytes exceeds {MAX_FILE_BYTES}"
        )
        return
    for description, pattern in FORBIDDEN_PATTERNS.items():
        if pattern.search(payload):
            findings.append(f"{label}: {description}")


def historical_blobs(root: Path, base: str) -> Iterable[tuple[str, bytes]]:
    for line in git_lines(root, "rev-list", "--objects", f"{base}..HEAD"):
        object_id, _, path = line.partition(" ")
        if not path:
            continue
        object_type = subprocess.run(
            ["git", "-C", str(root), "cat-file", "-t", object_id],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        ).stdout.strip()
        if object_type != "blob":
            continue
        payload = subprocess.run(
            ["git", "-C", str(root), "cat-file", "blob", object_id],
            check=True,
            capture_output=True,
        ).stdout
        yield f"history {object_id[:12]} {path}", payload


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--base", default=DEFAULT_BASE)
    args = parser.parse_args()

    root = args.repository.resolve()
    findings: list[str] = []
    for path in candidate_paths(root, args.base):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        scan_payload(findings, relative, path.suffix, path.read_bytes())

    for label, payload in historical_blobs(root, args.base):
        scan_payload(findings, label, Path(label).suffix, payload)

    metadata = subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "log",
            "--format=%H%n%an%n%ae%n%B",
            f"{args.base}..HEAD",
        ],
        check=True,
        capture_output=True,
    ).stdout
    scan_payload(findings, "fork commit metadata", "", metadata)

    if findings:
        raise SystemExit("\n".join(findings))
    print("repository hygiene: PASS")


if __name__ == "__main__":
    main()
