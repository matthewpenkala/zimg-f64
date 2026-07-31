"""Run the public fixture through one or more scaler executables."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

INPUT_BYTES = 99_532_800
QUANTIZED_BYTES = 18_662_400
PREQUANT_BYTES = 74_649_600


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_identity(path: Path) -> dict[str, int | str]:
    return {
        "path": str(path),
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def parse_executable(value: str) -> tuple[str, Path]:
    label, separator, path = value.partition("=")
    if not separator or not label or not path:
        raise argparse.ArgumentTypeError("expected LABEL=PATH")
    resolved = Path(path).resolve()
    if not resolved.is_file():
        raise argparse.ArgumentTypeError(f"executable not found: {resolved}")
    return label, resolved


def run_one(
    label: str,
    executable: Path,
    fixture: Path,
    threads: int,
    sequence: int,
    directory: Path,
) -> dict[str, Any]:
    quantized = directory / f"{label}-{threads}-{sequence}.raw"
    prequant = directory / f"{label}-{threads}-{sequence}.f64"
    argv = [
        str(executable),
        "--input",
        str(fixture),
        "--output",
        str(quantized),
        "--prequant-output",
        str(prequant),
        "--threads",
        str(threads),
        "--max-frames",
        "3",
        "--precision",
        "f64",
    ]
    completed = subprocess.run(
        argv,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode:
        raise RuntimeError(
            f"{label} failed with {completed.returncode}:\n{completed.stderr}"
        )
    if quantized.stat().st_size != QUANTIZED_BYTES:
        raise RuntimeError(f"{label}: unexpected quantized byte count")
    if prequant.stat().st_size != PREQUANT_BYTES:
        raise RuntimeError(f"{label}: unexpected prequant byte count")

    final_line: dict[str, Any] | None = None
    for line in completed.stderr.splitlines():
        if line.startswith("{") and '"precision":"f64"' in line:
            final_line = json.loads(line)
    if final_line is None:
        raise RuntimeError(f"{label}: missing scaler statistics")

    result = {
        "label": label,
        "threads": threads,
        "sequence": sequence,
        "executable": file_identity(executable),
        "argv": argv,
        "quantized": file_identity(quantized),
        "prequant": file_identity(prequant),
        "scaler_statistics": final_line,
    }
    quantized.unlink()
    prequant.unlink()
    return result


def atomic_json(path: Path, document: dict[str, Any]) -> None:
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(document, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument(
        "--executable",
        required=True,
        action="append",
        type=parse_executable,
        help="repeatable LABEL=PATH",
    )
    parser.add_argument("--n-threads", type=int, default=8)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--expected-quantized-sha256")
    parser.add_argument("--expected-prequant-sha256")
    args = parser.parse_args()

    fixture = args.fixture.resolve()
    if fixture.stat().st_size != INPUT_BYTES:
        raise RuntimeError(f"unexpected fixture byte count: {fixture.stat().st_size}")
    if args.n_threads < 1:
        raise ValueError("--n-threads must be positive")

    with tempfile.TemporaryDirectory(prefix="zimg-f64-determinism-") as directory:
        root = Path(directory)
        runs = []
        for label, executable in args.executable:
            runs.append(run_one(label, executable, fixture, 1, 0, root))
            runs.append(
                run_one(label, executable, fixture, args.n_threads, 0, root)
            )
            runs.append(
                run_one(label, executable, fixture, args.n_threads, 1, root)
            )

    quantized_hashes = {run["quantized"]["sha256"] for run in runs}
    prequant_hashes = {run["prequant"]["sha256"] for run in runs}
    if len(quantized_hashes) != 1 or len(prequant_hashes) != 1:
        raise RuntimeError("cross-build/thread output mismatch")

    quantized_sha = next(iter(quantized_hashes))
    prequant_sha = next(iter(prequant_hashes))
    if (
        args.expected_quantized_sha256
        and quantized_sha != args.expected_quantized_sha256.lower()
    ):
        raise RuntimeError("quantized SHA-256 differs from the frozen release hash")
    if (
        args.expected_prequant_sha256
        and prequant_sha != args.expected_prequant_sha256.lower()
    ):
        raise RuntimeError("prequant SHA-256 differs from the frozen release hash")

    document = {
        "schema": "zimg-f64-determinism/v1",
        "generated_utc": datetime.now(UTC).isoformat(),
        "pass": True,
        "fixture": file_identity(fixture),
        "quantized_bytes": QUANTIZED_BYTES,
        "prequant_bytes": PREQUANT_BYTES,
        "quantized_sha256": quantized_sha,
        "prequant_sha256": prequant_sha,
        "runs": runs,
    }
    if args.report:
        atomic_json(args.report, document)
    print(json.dumps(document, indent=2))


if __name__ == "__main__":
    main()
