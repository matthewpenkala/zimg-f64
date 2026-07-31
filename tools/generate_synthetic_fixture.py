"""Generate the redistributable three-frame 4K 4:2:2 10-bit release fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import BinaryIO, cast

import numpy as np
from numpy.typing import NDArray

WIDTH = 3840
HEIGHT = 2160
CHROMA_WIDTH = 1920
FRAMES = 3
EXPECTED_BYTES = FRAMES * (WIDTH * HEIGHT + 2 * CHROMA_WIDTH * HEIGHT) * 2


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_plane(handle: BinaryIO, plane: NDArray[np.integer]) -> None:
    values = np.asarray(plane, dtype="<u2")
    if values.ndim != 2 or values.shape[0] != HEIGHT:
        raise ValueError(f"unexpected plane shape: {values.shape}")
    values.tofile(handle)


def luma(frame: int) -> NDArray[np.uint16]:
    x = np.arange(WIDTH, dtype=np.uint32)[None, :]
    y = np.arange(HEIGHT, dtype=np.uint32)[:, None]

    if frame == 0:
        plane = ((x * 1023) // (WIDTH - 1) + (y % 17) * 3) & 1023
        plane = np.asarray(plane, dtype=np.uint16)
        constants = np.asarray([0, 64, 512, 940, 1023], dtype=np.uint16)
        for index, value in enumerate(constants):
            plane[index * 8 : (index + 1) * 8, :] = value
    elif frame == 1:
        plane = np.asarray(((x ^ y) & 1) * 1023, dtype=np.uint16)
        plane[0, :] = 0
        plane[-1, :] = 1023
        plane[:, 0] = 1023
        plane[:, -1] = 0
        plane[HEIGHT // 2, WIDTH // 2] = 1023
        plane[HEIGHT // 2, WIDTH // 2 + 1] = 0
    elif frame == 2:
        hashed = (
            x * np.uint32(0x45D9F3B)
            + y * np.uint32(0x119DE1F3)
            + (x * y) * np.uint32(17)
            + np.uint32(0x5A17C9E3)
        )
        plane = np.asarray((hashed ^ (hashed >> 16)) & 1023, dtype=np.uint16)
        plane[256:512, 512:1024] = 64
        plane[768:1024, 1536:2304] = 940
        plane[1280:1536, 2816:3328] = 512
    else:
        raise ValueError(frame)
    return cast("NDArray[np.uint16]", plane)


def chroma(frame: int, plane_index: int) -> NDArray[np.uint16]:
    x = np.arange(CHROMA_WIDTH, dtype=np.uint32)[None, :]
    y = np.arange(HEIGHT, dtype=np.uint32)[:, None]

    if frame == 0:
        if plane_index == 0:
            plane = (64 + (x * 896) // (CHROMA_WIDTH - 1) + y % 7) & 1023
        else:
            plane = (64 + (y * 896) // (HEIGHT - 1) + x % 7) & 1023
        plane = np.asarray(plane, dtype=np.uint16)
        plane[:16, :] = 512
    elif frame == 1:
        frequency = 1 if plane_index == 0 else 3
        plane = np.asarray(((x * frequency + y) & 1) * 1023, dtype=np.uint16)
        plane[HEIGHT // 2 - 1 : HEIGHT // 2 + 1, :] = 512
    elif frame == 2:
        seed = np.uint32(0x9E3779B9 if plane_index == 0 else 0x7F4A7C15)
        hashed = x * np.uint32(2246822519) + y * np.uint32(3266489917) + seed
        plane = np.asarray((hashed ^ (hashed >> 13)) & 1023, dtype=np.uint16)
        plane[300:600, 300:700] = 64 if plane_index == 0 else 960
        plane[1200:1500, 1100:1500] = 512
    else:
        raise ValueError(frame)
    return cast("NDArray[np.uint16]", plane)


def generate(output: Path) -> dict[str, int | str]:
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(output)

    with output.open("xb") as handle:
        for frame in range(FRAMES):
            write_plane(handle, luma(frame))
            write_plane(handle, chroma(frame, 0))
            write_plane(handle, chroma(frame, 1))

    byte_count = output.stat().st_size
    if byte_count != EXPECTED_BYTES:
        raise RuntimeError(f"fixture size {byte_count} != {EXPECTED_BYTES}")
    return {
        "path": str(output),
        "bytes": byte_count,
        "sha256": sha256_file(output),
        "frames": FRAMES,
        "format": "yuv422p10le",
        "dimensions": f"{WIDTH}x{HEIGHT}",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(generate(args.output), indent=2))


if __name__ == "__main__":
    main()
