# Binary64 reference components

These components are isolated from the public zimg API and do not replace
upstream resize execution paths.

## Programs

- `f64resize_yuv`: format-specific 3840×2160 4:2:2 10-bit to 1920×1080
  4:2:0 10-bit reference scaler.
- `f64_reference_probe`: line-oriented filter-coefficient and active-geometry
  probe used by independent Python oracles.
- `f64_mpfr_oracle`: MPFR-256/GMP coefficient oracle covering every row of the
  four validated production-plane contexts plus deterministic adversaries.

## Arithmetic

The resize path uses normalized binary64 coefficients, explicit `std::fma`,
binary64 inter-axis storage, and fixed tap order. There is no intermediate
quantization or clipping. The final integer store performs nearest-even
rounding and clamps only to the representable 10-bit container.

The reference application is intentionally not generalized beyond its tested
raw format and geometry. It writes directly to its output path; callers are
responsible for distinct input/output paths and transactional promotion.

