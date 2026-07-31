from __future__ import annotations

import math
import subprocess
from pathlib import Path

import mpmath as mp
import pytest
import sympy as sp
from hypothesis import given, settings
from hypothesis import strategies as st

mp.mp.dps = 100


def exact_float(value: float) -> mp.mpf:
    numerator, denominator = value.as_integer_ratio()
    return mp.mpf(numerator) / denominator


def spline36_mp(value: mp.mpf) -> mp.mpf:
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


def filter_row_mp(
    src_dim: int,
    dst_dim: int,
    shift: float,
    width: float,
    row: int,
) -> dict[int, mp.mpf]:
    scale_f = float(dst_dim) / width
    step_f = min(scale_f, 1.0)
    filter_size = max(math.ceil(3.0 / step_f) * 2, 1)
    pos_f = (float(row) + 0.5) / scale_f + shift
    rounding_input = pos_f - filter_size / 2.0
    if rounding_input < 0:
        begin_f = math.floor(rounding_input + 0.5) + 0.5
    else:
        begin_f = math.floor(
            rounding_input + math.nextafter(0.5, 0.0)
        ) + 0.5

    pos = exact_float(pos_f)
    step = exact_float(step_f)
    begin = exact_float(begin_f)
    weights = [
        spline36_mp((begin + index - pos) * step)
        for index in range(filter_size)
    ]
    total = mp.fsum(weights)
    folded: dict[int, mp.mpf] = {}
    for index, weight in enumerate(weights):
        xpos = begin + index
        if xpos < 0:
            real = -xpos
        elif xpos >= src_dim:
            real = 2 * src_dim - xpos
        else:
            real = xpos
        source_index = min(max(int(mp.floor(real)), 0), src_dim - 1)
        folded[source_index] = (
            folded.get(source_index, mp.mpf(0)) + weight / total
        )
    return folded


def run_probe(executable: Path, queries: list[str]) -> list[list[str]]:
    completed = subprocess.run(
        [str(executable)],
        input="\n".join(queries) + "\n",
        text=True,
        capture_output=True,
        check=True,
    )
    return [
        line.split() for line in completed.stdout.splitlines() if line.strip()
    ]


def parse_filter(tokens: list[str]) -> tuple[int, int, int, int, list[float]]:
    assert tokens[0] == "F"
    width, stride, left, input_width = map(int, tokens[1:5])
    coefficients = list(map(float, tokens[5:]))
    assert len(coefficients) == width
    assert stride >= width
    return width, stride, left, input_width, coefficients


def assert_filter_matches_oracle(
    src: int,
    dst: int,
    shift: float,
    width: float,
    row: int,
    tokens: list[str],
) -> None:
    _, _, left, input_width, coefficients = parse_filter(tokens)
    assert input_width == src
    oracle = filter_row_mp(src, dst, shift, width, row)
    nonzero = {index for index, value in oracle.items() if value != 0}
    assert not (nonzero - set(range(left, left + len(coefficients))))

    errors = []
    for offset, actual in enumerate(coefficients):
        reference = float(oracle.get(left + offset, mp.mpf(0)))
        errors.append(abs(actual - reference))
    assert max(errors, default=0.0) <= 1.0e-14
    assert abs(math.fsum(coefficients) - 1.0) <= 2.0e-14

    for constant in (0.0, 64.0, 512.0, 940.0, 1023.0):
        actual_dc = math.fsum(constant * value for value in coefficients)
        assert abs(actual_dc - constant) <= 2.5e-12


def test_sympy_exact_rational_kernel_identity() -> None:
    x = sp.symbols("x")
    p0 = (
        1
        - 3 * x / sp.Integer(209)
        - 453 * x**2 / sp.Integer(209)
        + 13 * x**3 / sp.Integer(11)
    )
    t = x - 1
    p1 = (
        -156 * t / sp.Integer(209)
        + 270 * t**2 / sp.Integer(209)
        - 6 * t**3 / sp.Integer(11)
    )
    u = x - 2
    p2 = (
        26 * u / sp.Integer(209)
        - 45 * u**2 / sp.Integer(209)
        + u**3 / sp.Integer(11)
    )
    assert sp.simplify(p0.subs(x, 0) - 1) == 0
    assert sp.simplify(p0.subs(x, 1)) == 0
    assert sp.simplify(p1.subs(x, 1)) == 0
    assert sp.simplify(p1.subs(x, 2)) == 0
    assert sp.simplify(p2.subs(x, 2)) == 0
    assert sp.simplify(p2.subs(x, 3)) == 0


def test_production_phases_and_borders_against_mpmath(
    probe_path: Path,
) -> None:
    contexts = [
        (3840, 1920, 0.0, 3840.0),
        (2160, 1080, 0.0, 2160.0),
        (1920, 960, -0.25, 1920.0),
        (2160, 540, 0.0, 2160.0),
    ]
    cases: list[tuple[int, int, float, float, int]] = []
    for src, dst, shift, width in contexts:
        rows = sorted(
            set(range(min(32, dst)))
            | set(range(max(0, dst - 32), dst))
            | {dst // 2}
        )
        cases.extend((src, dst, shift, width, row) for row in rows)
    queries = [
        f"F {src} {dst} {shift:.17g} {width:.17g} {row}"
        for src, dst, shift, width, row in cases
    ]
    replies = run_probe(probe_path, queries)
    assert len(replies) == len(cases)
    for case, reply in zip(cases, replies, strict=True):
        assert_filter_matches_oracle(*case, reply)


@settings(max_examples=220, deadline=None, derandomize=True, database=None)
@given(
    src=st.integers(min_value=1, max_value=48),
    dst=st.integers(min_value=1, max_value=48),
    width_half_units=st.integers(min_value=2, max_value=192),
    shift_eighth_units=st.integers(min_value=-24, max_value=24),
    row_seed=st.integers(min_value=0, max_value=255),
)
def test_random_filter_geometry_and_convolution(
    probe_path: Path,
    src: int,
    dst: int,
    width_half_units: int,
    shift_eighth_units: int,
    row_seed: int,
) -> None:
    width = width_half_units / 2.0
    shift = shift_eighth_units / 8.0
    row = row_seed % dst
    reply = run_probe(
        probe_path,
        [f"F {src} {dst} {shift:.17g} {width:.17g} {row}"],
    )[0]
    assert_filter_matches_oracle(src, dst, shift, width, row, reply)

    _, _, left, _, coefficients = parse_filter(reply)
    source = [
        float(((index * 811 + row * 313) ^ (index << 5)) % 1024)
        for index in range(src)
    ]
    actual = math.fsum(
        value * source[left + offset]
        for offset, value in enumerate(coefficients)
    )
    oracle = mp.fsum(
        value * source[index]
        for index, value in filter_row_mp(
            src, dst, shift, width, row
        ).items()
    )
    assert abs(actual - float(oracle)) <= 1.0e-10


def chroma_expected(
    width: int,
    height: int,
    active_left: float,
    active_top: float,
    active_width: float,
    active_height: float,
    subsample_w: int,
    subsample_h: int,
    location_w: str,
    location_h: str,
) -> tuple[float, ...]:
    ideal_w = 1.0 / (1 << subsample_w)
    ideal_h = 1.0 / (1 << subsample_h)
    chroma_width = width >> subsample_w
    chroma_height = height >> subsample_h
    actual_w = chroma_width / width
    actual_h = chroma_height / height
    offset_w = -0.5 + 0.5 * actual_w if location_w == "LEFT" else 0.0
    if location_h == "TOP":
        offset_h = -0.5 + 0.5 * actual_h
    elif location_h == "BOTTOM":
        offset_h = 0.5 - 0.5 * actual_h
    else:
        offset_h = 0.0
    return (
        chroma_width,
        chroma_height,
        active_left * ideal_w - offset_w,
        active_top * ideal_h - offset_h,
        active_width * ideal_w,
        active_height * ideal_h,
    )


def test_explicit_production_chroma_phase_derivation(
    probe_path: Path,
) -> None:
    query = (
        "G 3840 2160 0 0 3840 2160 1 0 LEFT CENTER "
        "1920 1080 0 0 1920 1080 1 1 LEFT CENTER"
    )
    values = list(map(float, run_probe(probe_path, [query])[0][1:]))
    assert values[2] == pytest.approx(0.25, abs=0.0)
    assert values[8] == pytest.approx(0.25, abs=0.0)
    assert values[12] == pytest.approx(-0.25, abs=0.0)
    assert values[14] == pytest.approx(0.0, abs=0.0)


def test_odd_dimension_siting_uses_actual_plane_ratio(
    probe_path: Path,
) -> None:
    query = (
        "G 5 7 0 0 5 7 1 1 LEFT TOP "
        "9 11 0 0 9 11 1 1 LEFT BOTTOM"
    )
    values = list(map(float, run_probe(probe_path, [query])[0][1:]))
    source = chroma_expected(5, 7, 0, 0, 5, 7, 1, 1, "LEFT", "TOP")
    destination = chroma_expected(
        9, 11, 0, 0, 9, 11, 1, 1, "LEFT", "BOTTOM"
    )
    scale_x = destination[4] / source[4]
    scale_y = destination[5] / source[5]
    expected = (
        *source,
        *destination,
        source[2] - destination[2] / scale_x,
        source[4] * destination[0] / destination[4],
        source[3] - destination[3] / scale_y,
        source[5] * destination[1] / destination[5],
    )
    assert values == pytest.approx(expected, rel=0.0, abs=2.0e-13)


@settings(max_examples=180, deadline=None, derandomize=True, database=None)
@given(
    src_w=st.integers(2, 384),
    src_h=st.integers(2, 384),
    dst_w=st.integers(2, 384),
    dst_h=st.integers(2, 384),
    src_sub_w=st.integers(0, 1),
    src_sub_h=st.integers(0, 1),
    dst_sub_w=st.integers(0, 1),
    dst_sub_h=st.integers(0, 1),
    src_location_w=st.sampled_from(["LEFT", "CENTER"]),
    src_location_h=st.sampled_from(["CENTER", "TOP", "BOTTOM"]),
    dst_location_w=st.sampled_from(["LEFT", "CENTER"]),
    dst_location_h=st.sampled_from(["CENTER", "TOP", "BOTTOM"]),
    src_left_q=st.integers(-8, 8),
    src_top_q=st.integers(-8, 8),
    dst_left_q=st.integers(-8, 8),
    dst_top_q=st.integers(-8, 8),
    src_trim=st.integers(0, 3),
    dst_trim=st.integers(0, 3),
)
def test_random_progressive_crop_and_siting_geometry(
    probe_path: Path,
    src_w: int,
    src_h: int,
    dst_w: int,
    dst_h: int,
    src_sub_w: int,
    src_sub_h: int,
    dst_sub_w: int,
    dst_sub_h: int,
    src_location_w: str,
    src_location_h: str,
    dst_location_w: str,
    dst_location_h: str,
    src_left_q: int,
    src_top_q: int,
    dst_left_q: int,
    dst_top_q: int,
    src_trim: int,
    dst_trim: int,
) -> None:
    source_active_width = max(0.25, float(src_w - src_trim))
    source_active_height = max(0.25, float(src_h - src_trim))
    destination_active_width = max(0.25, float(dst_w - dst_trim))
    destination_active_height = max(0.25, float(dst_h - dst_trim))
    source_left, source_top = src_left_q / 4.0, src_top_q / 4.0
    destination_left, destination_top = (
        dst_left_q / 4.0,
        dst_top_q / 4.0,
    )
    query = (
        f"G {src_w} {src_h} {source_left} {source_top} "
        f"{source_active_width} {source_active_height} "
        f"{src_sub_w} {src_sub_h} {src_location_w} {src_location_h} "
        f"{dst_w} {dst_h} {destination_left} {destination_top} "
        f"{destination_active_width} {destination_active_height} "
        f"{dst_sub_w} {dst_sub_h} {dst_location_w} {dst_location_h}"
    )
    values = list(map(float, run_probe(probe_path, [query])[0][1:]))
    source = chroma_expected(
        src_w,
        src_h,
        source_left,
        source_top,
        source_active_width,
        source_active_height,
        src_sub_w,
        src_sub_h,
        src_location_w,
        src_location_h,
    )
    destination = chroma_expected(
        dst_w,
        dst_h,
        destination_left,
        destination_top,
        destination_active_width,
        destination_active_height,
        dst_sub_w,
        dst_sub_h,
        dst_location_w,
        dst_location_h,
    )
    scale_x = destination[4] / source[4]
    scale_y = destination[5] / source[5]
    expected = (
        *source,
        *destination,
        source[2] - destination[2] / scale_x,
        source[4] * destination[0] / destination[4],
        source[3] - destination[3] / scale_y,
        source[5] * destination[1] / destination[5],
    )
    assert values == pytest.approx(expected, rel=0.0, abs=2.0e-13)


@pytest.mark.parametrize(
    "query",
    [
        "F 0 1 0 1 0",
        "F 1 0 0 1 0",
        "F 1 1 0 0 0",
        "F 1 1 0 -1 0",
        "F 1 1 0 1e-320 0",
        "F 4294967295 4294967295 1e308 1e-308 0",
        "G 1 1 0 0 1 1 1 1 LEFT TOP 2 2 0 0 2 2 1 1 LEFT TOP",
    ],
)
def test_invalid_and_overflow_prone_geometry_is_rejected(
    probe_path: Path,
    query: str,
) -> None:
    completed = subprocess.run(
        [str(probe_path)],
        input=query + "\n",
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode != 0
    assert "reference_probe:" in completed.stderr

