# Determinism and floating-point contract

## Output determinism

For the generated release fixture, the gate requires identical:

- quantized `yuv420p10le` bytes;
- prequantized little-endian binary64 plane bytes;
- byte counts;
- SHA-256 hashes.

The comparison spans independent MSVC, GCC, and Clang builds, one thread,
N threads, and repeated N-thread runs.

This is an output-reproducibility claim. It is not a claim that compiler
executables or object files are bit-reproducible.

## MSVC

The CMake release contract applies:

```text
/O2 /fp:strict /arch:AVX2 /W4 /permissive-
```

`/fp:strict` prevents unsafe reassociation and preserves the dynamic rounding
environment needed by final `std::lrint`.

## GCC and Clang

The CMake release contract applies:

```text
-O3 -mavx2
-fno-fast-math
-ffp-contract=off
-frounding-math
-fno-associative-math
-fno-finite-math-only
-Wall -Wextra -Wpedantic
```

No reciprocal approximation or finite-only assumption is enabled. Implicit
FMA contraction is disabled; explicit `std::fma` is the sole contraction
policy.

## Tap and thread order

Each output sample iterates taps in monotonically increasing table order.
Parallel work is partitioned only by output rows. A sample is never reduced
across workers, and thread-completion order cannot affect a sample.

Per-thread ringing/clamp statistics are merged only after pixel production;
their merge does not affect output bytes.

## Rounding

The program sets `FE_TONEAREST` before processing. Input 10-bit integers are
exactly representable in binary64. Final `std::lrint` therefore uses
nearest-even rounding under the required environment, followed by the sole
`[0, 1023]` representable-container clamp.

## Frozen fixture

The synthetic fixture is generated from documented integer expressions and a
fixed seed. The release records its SHA-256, byte count, generator hash, output
hashes, compiler identities, commands, and thread matrix in
[`VALIDATION.md`](../VALIDATION.md) and the machine-readable release record.

