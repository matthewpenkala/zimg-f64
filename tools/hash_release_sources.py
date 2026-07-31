"""Hash the fork-owned release source set without a self-referential record."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

DEFAULT_BASE = "1ad1895d5ff0bbe69c61243f9996aede713d1b5f"
EXCLUDED_RECORDS = {
    "VALIDATION.md",
    "validation/release-v0.1.0.json",
}


def changed_paths(root: Path, base: str) -> list[str]:
    completed = subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "diff",
            "--name-only",
            "--diff-filter=ACMR",
            base,
            "--",
        ],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return sorted(
        path
        for path in completed.stdout.splitlines()
        if path and path not in EXCLUDED_RECORDS
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--base", default=DEFAULT_BASE)
    args = parser.parse_args()

    root = args.repository.resolve()
    digest = hashlib.sha256()
    total_bytes = 0
    paths = changed_paths(root, args.base)
    for relative in paths:
        payload = (root / relative).read_bytes()
        encoded_path = relative.encode("utf-8")
        digest.update(encoded_path)
        digest.update(b"\0")
        digest.update(str(len(payload)).encode("ascii"))
        digest.update(b"\0")
        digest.update(payload)
        total_bytes += len(payload)

    print(
        json.dumps(
            {
                "schema": "zimg-f64-release-source-set/v1",
                "semantic_base": args.base,
                "excluded_self_referential_records": sorted(EXCLUDED_RECORDS),
                "file_count": len(paths),
                "bytes": total_bytes,
                "sha256": digest.hexdigest(),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
