# Numerical validation

The validation stack keeps production arithmetic and oracle arithmetic
separate.

## Production path

The reference scaler evaluates canonical Spline36 in IEEE-754 binary64 with
explicit `std::fma`, normalized binary64 coefficient tables, binary64
inter-axis storage, and no intermediate quantization or clamp.

## Independent coefficient oracles

`f64_mpfr_oracle` uses MPFR at 256-bit precision with GMP-backed arithmetic.
It freezes the pinned binary64 geometry decisions, then independently
evaluates:

- exact rational Spline36 constants;
- every nominal tap;
- normalization;
- reflection/folding;
- coefficient accumulation by source index.

It covers every destination row of the four format-specific plane contexts:

- 3840→1920 luma horizontal;
- 2160→1080 luma vertical;
- 1920→960 chroma horizontal with derived `-0.25` shift;
- 2160→540 chroma vertical;

plus deterministic small-dimension, active-width, dyadic-shift, and border
adversaries.

The release gate reports:

- absolute coefficient error;
- row-sum error;
- meaningful ULP distance away from kernel zeros and cancellation;
- geometry mismatches.

ULP distance is not treated as meaningful near exact zero or strong
cancellation; absolute error is the gate there.

## Python oracles

The pytest/Hypothesis suite independently uses:

- SymPy for exact-rational polynomial identities and segment boundaries;
- mpmath at 100 decimal digits for coefficient and convolution witnesses;
- NumPy for deterministic raw fixtures and memory-bounded plane access;
- SciPy for descriptive error-distribution statistics.

Deterministic Hypothesis settings cover:

- small dimensions and active-region crops;
- LEFT/CENTER and applicable vertical sitings;
- dyadic shifts/scales;
- constants at 0, nominal black, neutral chroma, nominal white, and 1023;
- impulses, ramps, checkerboards, alternating extrema, and border adversaries;
- code-domain headroom/footroom;
- invalid, non-finite, zero, and overflow-prone geometry.

## Convolution and final quantization

The generated public fixture supplies independent convolution witnesses across
all three planes and frames. mpmath reconstructs the separable two-axis
convolution at deterministic border, center, impulse, flat-field, and seeded
interior coordinates.

The gate reports maximum absolute and RMS prequantized error. Quantized
mismatches are separated into:

- non-tie mismatches: always a failure;
- exact/near half-code ties: classified independently because an arbitrarily
  small upstream arithmetic difference can select the opposite nearest-even
  result.

The release witness tool classifies a mismatch as boundary-adjacent only when
the 100-decimal-digit oracle lies within `1e-9` code values of an exact
half-code boundary. That radius is ten times the frozen `1e-10` maximum
convolution-error gate. Boundary-adjacent mismatches are reported but do not
hide a non-boundary mismatch; every non-boundary mismatch is fatal.

Thresholds are frozen in the tests and are not weakened to accommodate a
release.

## Ringing versus arithmetic error

Overshoot/undershoot is measured from the prequantized binary64 planes and
reported independently from coefficient or convolution error. Final
representable-container clamp counts are a third category.

The implementation does not clip intermediates or modify canonical
coefficients to conceal halos.
