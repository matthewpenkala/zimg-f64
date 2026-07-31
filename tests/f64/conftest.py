from __future__ import annotations

import os
from pathlib import Path

import pytest


def executable_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


@pytest.fixture(scope="session")
def build_directory() -> Path:
    value = os.environ.get("ZIMG_F64_BUILD_DIR")
    if not value:
        pytest.skip("set ZIMG_F64_BUILD_DIR to a completed CMake build directory")
    path = Path(value).resolve()
    if not path.is_dir():
        raise RuntimeError(f"build directory does not exist: {path}")
    return path


@pytest.fixture(scope="session")
def probe_path(build_directory: Path) -> Path:
    path = build_directory / executable_name("f64_reference_probe")
    if not path.is_file():
        raise RuntimeError(f"reference probe not found: {path}")
    return path


@pytest.fixture(scope="session")
def scaler_path(build_directory: Path) -> Path:
    path = build_directory / executable_name("f64resize_yuv")
    if not path.is_file():
        raise RuntimeError(f"reference scaler not found: {path}")
    return path

