# v0.1.0 validation record

This record describes the public, deterministic release fixture and the
release gates applied to the exact commit targeted by annotated tag
`v0.1.0`. Machine-readable evidence is in
[`validation/release-v0.1.0.json`](validation/release-v0.1.0.json).

The tag target is the authoritative commit identity. A commit cannot embed its
own SHA-1 without changing that SHA-1, so the record names the tag and also
provides a reproducible digest of every fork-owned release file except these
two self-referential validation records.

## Gate summary

| Gate | v0.1.0 result |
|---|---|
| Upstream regression | **PASS** — 162/162 tests in 20 suites |
| MSVC/GCC/Clang build | **PASS** — three independent strict-FP builds |
| Quantized determinism | **PASS** — identical bytes and SHA-256 |
| Prequant determinism | **PASS** — identical bytes and SHA-256 |
| 1/N/repeated-N threads | **PASS** — 1, 32, and repeated 32 threads |
| MPFR-256/GMP | **PASS** — zero geometry mismatch; thresholds met |
| mpmath/SymPy/Hypothesis | **PASS** — 16/16 per compiler; witnesses pass |
| Sanitizers | **PASS** — local Clang ASan/UBSan/integer and MSVC ASan; release additionally requires green Linux GCC/Clang sanitizer CI |
| Static analysis/warnings | **PASS** — cppcheck, clang-tidy, Ruff, mypy, and high-warning builds |
| Raw/FFmpeg integration | **PASS** — exact three-frame input/output framing and full decode |
| Privacy/secrets/large objects | **PASS** — custom history scan and Gitleaks; no blob over 1 MB |
| License/provenance | **PASS** — upstream/submodule terms preserved and documented |

The tag and GitHub release are created only after every job in
`.github/workflows/f64-validation.yml`, including Linux GCC and Clang
sanitizer jobs, passes on this same commit. Tag pushes deliberately do not
repeat the matrix: the annotated tag must resolve to the already validated
release-branch commit.

## Provenance

| Item | Identity |
|---|---|
| Canonical upstream | `https://github.com/sekrit-twc/zimg` |
| Semantic base | `1ad1895d5ff0bbe69c61243f9996aede713d1b5f` |
| graphengine | `cb5b2ce13384ec2491f0c37256ea210034799f69` |
| graphengine GoogleTest | `e2239ee6043f73722e7aa812a459f54a28552929` |
| zimg test GoogleTest | `6910c9d9165801d8827d628cb72eb7ea9dd538c5` |
| Fork-owned source-set files | 37 |
| Fork-owned source-set bytes | 151,370 |
| Fork-owned source-set SHA-256 | `6386e5a1ba3e4773136a9d526a2b149ecde15385aac6b4b77d50554765361449` |

## Builds and floating-point contract

Local release evidence was produced on Windows 11 x64 build 26200.

| Build | Compiler | Reference executable bytes | Reference executable SHA-256 |
|---|---|---:|---|
| MSVC | 19.44.35227.0 | 96,768 | `56344990ac4826728acd868d728b27a9dc9c2ba6d70fa6c3c42a1fa155e6bb2e` |
| GCC | 16.1.0 | 335,610 | `a620001bff74f81763d2c13c16da58fb22f935308a68d03167ec170aada9df6c` |
| Clang | 22.1.8 | 320,612 | `9b168372e00214a56b4030322cd6d5bd59a1cc08b0823eb75441b889854450fd` |

Executable identity is recorded for local provenance only. Executable
bit-reproducibility is not claimed.

MSVC used:

```text
/O2 /fp:strict /arch:AVX2 /W4 /permissive-
```

GCC and Clang used:

```text
-O3 -mavx2 -fno-fast-math -ffp-contract=off -frounding-math
-fno-associative-math -fno-finite-math-only -Wall -Wextra -Wpedantic
```

The only fused operation is explicit `std::fma`. Tap order is invariant.
Parallel work is row-only. Final rounding is `FE_TONEAREST`/nearest-even,
followed by the sole representable-container clamp to `[0, 1023]`.

## Public fixture and output identity

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| 3840×2160 `yuv422p10le`, three frames | 99,532,800 | `a820c2c08a91f203d7b7cf6df0f01573ebb8365a31e67a61de3313baf8c95c21` |
| 1920×1080 `yuv420p10le`, three frames | 18,662,400 | `914477670148e89188aee144a57629bdef0ca41c8c9d2f4731752cf46dd811ca` |
| prequantized planar binary64, three frames | 74,649,600 | `cc07600cdc80284243b1efbf8c4cb59a4cea23152c9cac08627ec274bf07e343` |

All nine release runs—MSVC, GCC, and Clang at one thread, 32 threads,
and repeated 32 threads—produced those two output hashes and byte counts.

## Numerical validation

### MPFR-256/GMP coefficient oracle

- rows: 8,404;
- coefficients: 88,916;
- geometry mismatches: 0;
- maximum absolute coefficient error: `8.8817841970012523e-16`;
- maximum meaningful ULP distance: 18;
- maximum row-sum error: `9.9920072216264089e-16`.

GCC and Clang oracle executables returned identical results.

### mpmath convolution witnesses

- decimal precision: 100 digits;
- witnesses: 468;
- maximum absolute prequantized error: `6.821210263296962e-13`;
- RMS error: `1.6870690915558896e-13`;
- mean error: `9.633912850756393e-14`;
- non-tie quantization mismatches: 0;
- exact/near-half-code boundary cases classified separately: 86;
- boundary classification radius: `1e-9` code values.

### Property and symbolic tests

The deterministic pytest/Hypothesis/SymPy/mpmath suite passed 16/16 tests
independently against MSVC, GCC, and Clang builds. Coverage includes the
production phases and borders, generic crop/siting geometry, odd dimensions,
constant fields, impulses, ramps, checkerboards, alternating extrema, seeded
adversaries, headroom/footroom, invalid inputs, and overflow-prone geometry.

## Ringing and clipping

The public alternating-extreme fixture deliberately provokes canonical
Spline36 negative-lobe excursions:

| Plane | Input range | Prequantized range | Low final clamps | High final clamps |
|---|---|---|---:|---:|
| Y′ | 0…1023 | `-167.951053340`…`1176.343809022` | 14,622 | 15,332 |
| Cb | 0…1023 | `26.843497739`…`969.818030979` | 0 | 0 |
| Cr | 0…1023 | `64.882234837`…`1002.118739233` | 0 | 0 |

These excursions are kernel behavior, not arithmetic error. They survive both
binary64 axes and are clipped only at final container storage.

## Sanitizers and analysis

- Clang 22.1.8: ASan, UBSan, and integer sanitizers; full three-frame
  1/32/repeated-32-thread workload; no finding.
- MSVC 19.44.35227.0: ASan; same workload; no finding.
- Linux CI: independent GCC ASan/UBSan and Clang ASan/UBSan/integer jobs are a
  mandatory release gate.
- cppcheck 2.21.0: pass.
- clang-tidy 22.1.8: pass with bugprone, CERT, Clang Static Analyzer,
  performance, and portability profiles.
- Ruff 0.16.1 and mypy 2.3.0 strict mode: pass.
- actionlint 1.7.12: workflow pass.

Reviewed inherited exceptions are narrow and documented in source/build
configuration: upstream Q14 conversions are bounded by immediately adjacent
assertions; upstream `FilterContext` is aggregate-initialized by its factory;
and MSVC C4670/C4673 misdiagnose upstream's private `std::runtime_error` base
despite catches through public `zimg::error::Exception`.

The pinned upstream musl-libm fixture emits one GCC
`-Wmaybe-uninitialized` warning for `fq`; it is unchanged upstream test code,
not compiled into the fork reference application.

## Integration and repository safety

- FFmpeg 8.1.2 fully decoded the three-frame input and output raw streams with
  no framing or decode error.
- Gitleaks 8.30.1 scanned the complete upstream-plus-fork history reachable
  from the release candidate (approximately 5.36 MB) with no finding.
- The fork-specific scanner checked current files, fork-owned historical
  blobs, commit metadata, private identifiers, local paths, generated media,
  forbidden binary suffixes, secrets, and size limits with no finding.
- Full-history and fork-history object inspection found zero blobs over
  1,000,000 bytes.
- Root zimg and graphengine retain WTFPL v2; both GoogleTest submodules retain
  their BSD-style licenses.

No private media, derived videos, raw project planes, source-derived private
hashes, screenshots, project identifiers, local absolute paths, credentials,
temporary logs, quarantined files, or platform binaries are part of the
release.
