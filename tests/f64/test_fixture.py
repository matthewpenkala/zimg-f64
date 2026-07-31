from __future__ import annotations

import importlib.util
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / "tools" / "generate_synthetic_fixture.py"
SPEC = importlib.util.spec_from_file_location("synthetic_fixture", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def test_luma_fixture_planes_cover_container_and_nominal_codes() -> None:
    for frame in range(GENERATOR.FRAMES):
        plane = GENERATOR.luma(frame)
        assert plane.shape == (GENERATOR.HEIGHT, GENERATOR.WIDTH)
        assert plane.dtype == np.uint16
        assert int(plane.min()) == 0
        assert int(plane.max()) == 1023
    first = GENERATOR.luma(0)
    for code in (0, 64, 512, 940, 1023):
        assert np.any(first == code)


def test_chroma_fixture_planes_cover_extrema_and_neutral() -> None:
    for frame in range(GENERATOR.FRAMES):
        for plane_index in (0, 1):
            plane = GENERATOR.chroma(frame, plane_index)
            assert plane.shape == (
                GENERATOR.HEIGHT,
                GENERATOR.CHROMA_WIDTH,
            )
            assert plane.dtype == np.uint16
            assert int(plane.min()) >= 0
            assert int(plane.max()) <= 1023
    assert np.any(GENERATOR.chroma(0, 0) == 512)
    assert np.any(GENERATOR.chroma(1, 1) == 0)
    assert np.any(GENERATOR.chroma(1, 1) == 1023)


def test_expected_fixture_byte_count() -> None:
    assert GENERATOR.EXPECTED_BYTES == 99_532_800

