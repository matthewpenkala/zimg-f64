# Algorithm and binary64 contract

## Scope

The fork retains the pinned zimg resize geometry and adds an opt-in
`FilterContextF64`. It does not replace the existing `FilterContext`, f32
execution paths, signed-Q14 tables, public graph API, or SIMD kernels.
Valid-input f32/Q14 output remains unchanged; malformed geometry now fails
explicitly instead of relying on a debug-only assertion.

The reference executable is intentionally restricted to progressive planar
3840×2160 `yuv422p10le` input and 1920×1080 `yuv420p10le` output.

## Canonical Spline36 formula

For `x = abs(x)`:

```text
0 <= x < 1:
    1 - (3/209)x - (453/209)x² + (13/11)x³

1 <= x < 2, t = x - 1:
    -(156/209)t + (270/209)t² - (6/11)t³

2 <= x < 3, u = x - 2:
    (26/209)u - (45/209)u² + (1/11)u³

x >= 3:
    0
```

Those rational constants are the canonical upstream formula. The production
path evaluates the expression in binary64; it does not use rational or MPFR
arithmetic.

## Mapping, support, and tap selection

For destination row/sample `i`, active input width `width`, source dimension
`src_dim`, and destination dimension `dst_dim`:

```text
scale        = dst_dim / width
step         = min(scale, 1)
support      = 3 / step
filter_size  = max(2 * ceil(support), 1)
pos          = (i + 0.5) / scale + shift
begin_pos    = round_halfup(pos - filter_size / 2) + 0.5
```

This is zimg's half-pixel-center mapping and translation-invariant tap
selection. Downscaling widens the kernel through `step`.

Each nominal tap is evaluated as:

```text
w[j] = Spline36((begin_pos + j - pos) * step)
```

The nominal weights are normalized by their binary64 sum before border
folding.

## Border behavior

The pinned behavior reflects a nominal sample once at the boundary:

```text
x < 0        -> -x
x >= width   -> 2 * width - x
```

If a far-out support position remains outside the plane, it is clamped to the
nearest endpoint. Coefficients that resolve to the same source index are
accumulated there. This is coefficient folding, not edge-value padding.

The fork retains that behavior. It does not alter coefficients to suppress
ringing.

## Axis order

The reference application reproduces the cost comparison in pinned
`resize/resize.cpp`:

```text
horizontal-first cost =
    max(xscale, 1) * 2 + xscale * max(yscale, 1)

vertical-first cost =
    max(yscale, 1) + yscale * max(xscale, 1) * 2
```

The validated 3840×2160→1920×1080 luma and direct chroma contexts select
vertical first. The executable verifies that decision at startup.

## Binary64 execution

The reference path uses binary64 for:

- kernel/coefficient evaluation;
- normalized coefficient storage;
- multiply-add accumulation;
- vertical-to-horizontal intermediate planes;
- prequantized output witnesses.

Every convolution tap is accumulated in index order through explicit
`std::fma`. Implicit contraction is disabled by the build contract.

Rows may execute concurrently, but one output sample is computed by exactly
one thread with an invariant tap order.

## Final quantization

No intermediate result is quantized or clamped. The final store:

1. executes with the process rounding mode set to `FE_TONEAREST`;
2. uses `std::lrint`, giving nearest-even behavior under that mode;
3. clamps only to the representable 10-bit container `[0, 1023]`;
4. records low/high clamp counts separately from arithmetic error.

Nominal legal-range limiting is not part of this scaler.

## Ringing

Spline36 has negative lobes. An exact evaluation of the canonical kernel can
overshoot or undershoot near high-contrast structure. Binary64 reduces
arithmetic error; it cannot remove that mathematical behavior.
