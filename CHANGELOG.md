# Changelog

All notable fork-specific changes are documented here. Upstream history
before the semantic-base commit remains available in Git.

## Unreleased - 2026-09-22

- Merged upstream `master` through `67e0603271c080e22c8429856dd4a8a56587e61e`
  without rebasing, preserving both upstream histories and the `v0.1.0` tag.
- Integrated the upstream explicit chromatic-adaptation option pass-through
  in graph construction and missing `<exception>` includes for libc++ 23.
- No changes to the binary64 Spline36 filter, geometry, resampler, or frozen
  synthetic output hashes.

## 0.1.0 - 2026-07-30

### Added

- Opt-in binary64 filter context derived from the canonical zimg filter matrix.
- Format-specific nonlinear planar Y′CbCr binary64 Spline36 reference scaler.
- Generic progressive active-region and chroma-siting geometry helpers.
- Compiler-independent coefficient/geometry probe.
- MPFR-256/GMP, mpmath, SymPy, NumPy/SciPy validation paths.
- Deterministic synthetic motion-graphics-style adversarial fixture generator.
- Cross-compiler and cross-thread output-identity gate.
- CMake build for MSVC, GCC, and Clang reference components.
- Sanitizer, static-analysis, upstream-regression, and GitHub Actions support.
- Algorithm, chroma, numerical, determinism, integration, provenance, and
  licensing documentation.

### Preserved

- Pinned upstream f32/Q14 filter conversion behavior and unit-test results.
- Canonical Spline36 constants, geometry, normalization, and border semantics.

### Limitations

- The reference scaler supports only the documented 4K 4:2:2 10-bit to 1080p
  4:2:0 10-bit raw conversion.
- Binary64 precision does not remove Spline36 ringing.
- No delivery codec or container is part of the core definition.
- Executable-file reproducibility is not claimed.

