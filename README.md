# zimg-f64

**zimg-phase/geometry-compatible canonical Spline36 evaluated in IEEE-754
binary64.**

This maintained fork adds an isolated reference path for studying how zimg's
canonical Spline36 resize semantics behave when coefficient evaluation,
coefficient storage, convolution accumulation, and inter-axis storage remain
in binary64.

The semantic reference is [`sekrit-twc/zimg`](https://github.com/sekrit-twc/zimg)
at commit
[`1ad1895d5ff0bbe69c61243f9996aede713d1b5f`](https://github.com/sekrit-twc/zimg/commit/1ad1895d5ff0bbe69c61243f9996aede713d1b5f).
The graphengine submodule remains pinned to
`cb5b2ce13384ec2491f0c37256ea210034799f69`.

The development branch subsequently merged upstream `master` through
[`67e0603271c080e22c8429856dd4a8a56587e61e`](https://github.com/sekrit-twc/zimg/commit/67e0603271c080e22c8429856dd4a8a56587e61e).
This incorporates an upstream chromatic-adaptation option pass-through and
two libc++ include fixes; it does not change the binary64 resize path or the
historical `v0.1.0` tag. See [upstream-sync provenance](docs/PROVENANCE.md#subsequent-upstream-integration).

## What this is—and is not

The Spline36 formula retains the rational constants used by the pinned
upstream implementation. Sample geometry, half-pixel-center mapping,
translation-invariant tap selection, downscale widening, normalization,
reflection/folding, and axis-order decisions follow that pinned reference.

This project is **not** upstream zimg, bit-exact zimg, an upstream-supported
f64 path, or exact-rational production arithmetic. MPFR, mpmath, and SymPy are
independent validation oracles. Upstream binary32 and signed-Q14 paths remain
distinct references whose valid-input output is unchanged.

Greater arithmetic precision does not remove Spline36's intrinsic
negative-lobe ringing. Overshoot and undershoot are preserved through both
axes and reported separately from arithmetic error.

## Reference data path

The included `f64resize_yuv` executable is deliberately format-specific:

- input: progressive planar `yuv422p10le`, 3840×2160;
- output: progressive planar `yuv420p10le`, 1920×1080;
- luma: 3840×2160 → 1920×1080;
- existing Cb/Cr planes: 1920×2160 → 960×540 directly;
- signal domain: nonlinear planar Y′CbCr;
- no RGB, linear-light, or 4:4:4 detour;
- no sharpening, denoising, intermediate clamp, or intermediate quantization;
- final store: round-to-nearest-even, then clamp to `[0, 1023]`.

The path is a reference implementation, not a general-purpose video
converter. FFmpeg and delivery codecs remain downstream integration concerns.

## Chroma phase

For the validated full-frame progressive 4:2:2 LEFT → 4:2:0 LEFT case:

```text
source chroma active_left       = +0.25
destination chroma active_left  = +0.25
horizontal active scale         = 0.5

shift = source_left - destination_left / scale
      = 0.25 - 0.25 / 0.5
      = -0.25 source-chroma samples

vertical shift = 0
```

`-0.25` is derived for that exact geometry. It is not a universal constant for
other crops, active regions, subsampling layouts, or chroma sitings. The
original validation inputs did not carry authoritative chroma-location
metadata; LEFT was an explicit source-interpretation assumption.

## Build

Initialize submodules first:

```sh
git submodule update --init --recursive
```

### GCC or Clang

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DZIMG_F64_BUILD_MPFR_ORACLE=ON
cmake --build build
```

### MSVC

```powershell
cmake -S . -B build-msvc -A x64 `
  -DZIMG_F64_BUILD_MPFR_ORACLE=OFF
cmake --build build-msvc --config Release
```

The CMake target applies the validated floating-point contract:

- MSVC: `/fp:strict`, `/arch:AVX2`, explicit `std::fma`;
- GCC/Clang: no fast-math, reassociation, finite-only assumptions, or implicit
  contraction; explicit `std::fma` is the only fused operation;
- tap accumulation order is fixed;
- parallelism is row-only, so scheduling cannot change a sample.

Executable bytes are not claimed reproducible. The release gate requires
byte-identical quantized and prequantized outputs across MSVC, GCC, Clang,
one thread, repeated N-thread runs, and fresh independent builds.

## Validate

Install the optional Python validation environment:

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install -e ".[validation]"
```

Then run:

```sh
pytest tests/f64
python tools/generate_synthetic_fixture.py --output fixture.yuv
python tools/run_determinism.py \
  --fixture fixture.yuv \
  --executable reference=build/f64resize_yuv
```

The raw fixture is generated deterministically and is never stored in Git.
See [VALIDATION.md](VALIDATION.md) for the frozen release evidence.

## Documentation

- [Algorithm and arithmetic contract](docs/ALGORITHM.md)
- [Chroma and active-region geometry](docs/CHROMA_GEOMETRY.md)
- [Numerical validation](docs/NUMERICAL_VALIDATION.md)
- [Determinism](docs/DETERMINISM.md)
- [Raw/FFmpeg integration](docs/INTEGRATION.md)
- [Upstream provenance](docs/PROVENANCE.md)
- [Licensing and dependencies](docs/LICENSING.md)
- [Original upstream README](docs/UPSTREAM_README.md)
- [Changelog](CHANGELOG.md)

## License

The upstream `COPYING` file and source notices are preserved. New fork-owned
source, test, build, and documentation files are offered under the same
WTFPL v2 terms unless a file states otherwise. Optional build and validation
dependencies are not vendored and retain their own licenses.
