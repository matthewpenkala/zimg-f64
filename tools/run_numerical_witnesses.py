"""Compare public-fixture convolutions and quantization with a 100-dps oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import subprocess
import tempfile
from pathlib import Path
from typing import Any, cast

import mpmath as mp
import numpy as np
from scipy import stats

mp.mp.dps = 100

SOURCE_COUNTS = (3840 * 2160, 1920 * 2160, 1920 * 2160)
DESTINATION_COUNTS = (1920 * 1080, 960 * 540, 960 * 540)
SOURCE_FRAME_SAMPLES = sum(SOURCE_COUNTS)
DESTINATION_FRAME_SAMPLES = sum(DESTINATION_COUNTS)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def exact_float(value: float) -> mp.mpf:
    numerator, denominator = value.as_integer_ratio()
    return mp.mpf(numerator) / denominator


def spline36(value: mp.mpf) -> mp.mpf:
    x = abs(value)
    if x < 1:
        return 1 + x * (
            -mp.mpf(3) / 209
            + x * (-mp.mpf(453) / 209 + x * mp.mpf(13) / 11)
        )
    if x < 2:
        x -= 1
        return x * (
            -mp.mpf(156) / 209
            + x * (mp.mpf(270) / 209 - x * mp.mpf(6) / 11)
        )
    if x < 3:
        x -= 2
        return x * (
            mp.mpf(26) / 209
            + x * (-mp.mpf(45) / 209 + x / 11)
        )
    return mp.mpf(0)


def filter_row(
    source_dimension: int,
    destination_dimension: int,
    shift: float,
    row: int,
) -> dict[int, mp.mpf]:
    scale_f = destination_dimension / float(source_dimension)
    step_f = min(scale_f, 1.0)
    filter_size = max(math.ceil(3.0 / step_f) * 2, 1)
    position_f = (row + 0.5) / scale_f + shift
    rounding_input = position_f - filter_size / 2.0
    if rounding_input < 0:
        begin_f = math.floor(rounding_input + 0.5) + 0.5
    else:
        begin_f = math.floor(
            rounding_input + math.nextafter(0.5, 0.0)
        ) + 0.5

    position = exact_float(position_f)
    step = exact_float(step_f)
    begin = exact_float(begin_f)
    weights = [
        spline36((begin + index - position) * step)
        for index in range(filter_size)
    ]
    total = mp.fsum(weights)
    folded: dict[int, mp.mpf] = {}
    for index, weight in enumerate(weights):
        nominal = begin + index
        if nominal < 0:
            reflected = -nominal
        elif nominal >= source_dimension:
            reflected = 2 * source_dimension - nominal
        else:
            reflected = nominal
        source_index = min(
            max(int(mp.floor(reflected)), 0),
            source_dimension - 1,
        )
        folded[source_index] = (
            folded.get(source_index, mp.mpf(0)) + weight / total
        )
    return folded


def run_scaler(
    scaler: Path,
    fixture: Path,
    quantized: Path,
    prequant: Path,
) -> dict[str, Any]:
    completed = subprocess.run(
        [
            str(scaler),
            "--input",
            str(fixture),
            "--output",
            str(quantized),
            "--prequant-output",
            str(prequant),
            "--threads",
            "1",
            "--max-frames",
            "3",
            "--precision",
            "f64",
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr)
    for line in completed.stderr.splitlines():
        if line.startswith("{") and '"precision":"f64"' in line:
            return cast(dict[str, Any], json.loads(line))
    raise RuntimeError("scaler statistics were not emitted")


def validate(
    scaler: Path,
    fixture: Path,
) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="zimg-f64-witness-") as directory:
        root = Path(directory)
        quantized_path = root / "quantized.raw"
        prequant_path = root / "prequant.f64"
        scaler_statistics = run_scaler(
            scaler, fixture, quantized_path, prequant_path
        )

        source = np.memmap(fixture, dtype="<u2", mode="r")
        prequant = np.memmap(prequant_path, dtype="<f8", mode="r")
        quantized = np.memmap(quantized_path, dtype="<u2", mode="r")
        generator = random.Random(0xF64A1E9)

        errors: list[float] = []
        non_tie_mismatches = 0
        tie_mismatches = 0
        witnesses = 0
        for frame in range(3):
            source_offsets = np.cumsum(
                (frame * SOURCE_FRAME_SAMPLES, *SOURCE_COUNTS[:-1])
            )
            destination_offsets = np.cumsum(
                (
                    frame * DESTINATION_FRAME_SAMPLES,
                    *DESTINATION_COUNTS[:-1],
                )
            )
            for plane in range(3):
                if plane == 0:
                    source_width, source_height = 3840, 2160
                    destination_width, destination_height = 1920, 1080
                    horizontal_shift = 0.0
                else:
                    source_width, source_height = 1920, 2160
                    destination_width, destination_height = 960, 540
                    horizontal_shift = -0.25

                source_plane = source[
                    source_offsets[plane] : source_offsets[plane]
                    + SOURCE_COUNTS[plane]
                ].reshape(source_height, source_width)
                prequant_plane = prequant[
                    destination_offsets[plane] : destination_offsets[plane]
                    + DESTINATION_COUNTS[plane]
                ].reshape(destination_height, destination_width)
                quantized_plane = quantized[
                    destination_offsets[plane] : destination_offsets[plane]
                    + DESTINATION_COUNTS[plane]
                ].reshape(destination_height, destination_width)

                points = {
                    (0, 0),
                    (1, 0),
                    (0, 1),
                    (1, 1),
                    (destination_width - 1, 0),
                    (0, destination_height - 1),
                    (destination_width - 1, destination_height - 1),
                    (destination_width // 2, destination_height // 2),
                }
                while len(points) < 52:
                    points.add(
                        (
                            generator.randrange(destination_width),
                            generator.randrange(destination_height),
                        )
                    )

                for x, y in points:
                    horizontal = filter_row(
                        source_width,
                        destination_width,
                        horizontal_shift,
                        x,
                    )
                    vertical = filter_row(
                        source_height,
                        destination_height,
                        0.0,
                        y,
                    )
                    oracle = mp.fsum(
                        vertical_weight
                        * horizontal_weight
                        * int(source_plane[source_y, source_x])
                        for source_y, vertical_weight in vertical.items()
                        for source_x, horizontal_weight in horizontal.items()
                    )
                    actual = float(prequant_plane[y, x])
                    errors.append(actual - float(oracle))
                    witnesses += 1

                    expected = min(max(int(mp.nint(oracle)), 0), 1023)
                    if int(quantized_plane[y, x]) != expected:
                        distance = abs(
                            oracle - (mp.floor(oracle) + mp.mpf("0.5"))
                        )
                        if distance <= mp.mpf("1e-9"):
                            tie_mismatches += 1
                        else:
                            non_tie_mismatches += 1
                del source_plane
                del prequant_plane
                del quantized_plane

        error_array = np.asarray(errors, dtype=np.float64)
        description = stats.describe(error_array)
        maximum_absolute_error = float(np.max(np.abs(error_array)))
        rms_error = float(np.sqrt(np.mean(error_array * error_array)))
        del source
        del prequant
        del quantized
        if maximum_absolute_error > 1.0e-10:
            raise RuntimeError("maximum convolution error exceeds threshold")
        if rms_error > 1.0e-11:
            raise RuntimeError("RMS convolution error exceeds threshold")
        if non_tie_mismatches:
            raise RuntimeError("non-tie quantization mismatch")

        return {
            "schema": "zimg-f64-convolution-witness/v1",
            "pass": True,
            "fixture_sha256": sha256_file(fixture),
            "scaler_sha256": sha256_file(scaler),
            "witnesses": witnesses,
            "maximum_absolute_error": maximum_absolute_error,
            "rms_error": rms_error,
            "mean_error": float(description.mean),
            "variance": float(description.variance),
            "minimum_error": float(description.minmax[0]),
            "maximum_error": float(description.minmax[1]),
            "non_tie_quantization_mismatches": non_tie_mismatches,
            "near_half_code_tie_mismatches": tie_mismatches,
            "near_half_code_classification_radius": 1.0e-9,
            "scaler_statistics": scaler_statistics,
        }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scaler", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    args = parser.parse_args()
    print(
        json.dumps(
            validate(args.scaler.resolve(), args.fixture.resolve()),
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
