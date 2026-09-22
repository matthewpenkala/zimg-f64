# Provenance

## Canonical upstream

- Repository: <https://github.com/sekrit-twc/zimg>
- Semantic base: `1ad1895d5ff0bbe69c61243f9996aede713d1b5f`
- Graphengine: `cb5b2ce13384ec2491f0c37256ea210034799f69`
- Graphengine googletest:
  `e2239ee6043f73722e7aa812a459f54a28552929`
- zimg test googletest:
  `6910c9d9165801d8827d628cb72eb7ea9dd538c5`

The fork preserves upstream Git history. The canonical upstream remote should
remain:

```sh
git remote add upstream https://github.com/sekrit-twc/zimg.git
```

Never push to `upstream`.

## Subsequent upstream integration

On 2026-09-22, the development branch merged upstream `master` at
`67e0603271c080e22c8429856dd4a8a56587e61e`. This is an additional
upstream integration, **not** a change to the pinned semantic reference above
or to the immutable `v0.1.0` release tag. The merge preserves ancestry from
both the fork and upstream; it is not a squash, rebase, or source dump.

Upstream had rewritten its recent `master` history. A `git range-diff` from
common predecessor `b8bd3d9` showed that its pixel-format and sRGB-gamma test
commits were patch-equivalent to commits already in this fork. The rewritten
chromatic-adaptation commit additionally passes the explicit
`chromatic_adaptation` option from `GraphBuilder` to `ColorspaceConversion`;
the default remains disabled. The fourth upstream commit adds `<exception>`
includes in the C API and tile example for libc++ 23. Relative to the
pre-merge fork tip, the complete source-tree change is exactly these three
added lines. Neither the canonical Spline36 implementation nor the fork's
binary64 resize path changes.

The zimg test googletest submodule remains at
`6910c9d9165801d8827d628cb72eb7ea9dd538c5`. The graphengine and its
googletest commits remain as recorded above. The local merge commit was
`1da640213bd6b546d41a14c6406a994e6f7f5577`; later documentation-only
commits can be audited separately.

## Fork changes

The minimal numerical extension:

1. factors canonical filter-matrix construction so both legacy and f64
   contexts consume the same matrix;
2. preserves upstream f32/Q14 conversion behavior through the unchanged
   `FilterContext` path;
3. adds opt-in `FilterContextF64` normalized binary64 storage;
4. adds explicit malformed/non-finite/overflow/empty-filter rejection;
5. adds a format-specific binary64 reference resampler;
6. adds generic progressive active-region and chroma-siting geometry helpers;
7. adds compiler-independent probes and MPFR/Python oracles;
8. adds public synthetic validation, reproducible builds, documentation, and
   CI.

The f64 path is not exposed as an upstream-supported public zimg graph option.

## Clean-reference procedure

To audit the fork against the semantic base:

```sh
git fetch upstream
git diff --submodule=log \
  1ad1895d5ff0bbe69c61243f9996aede713d1b5f..HEAD
git submodule status --recursive
```

The release gate runs the pinned upstream unit suite from the publication
candidate and separately validates the fork-owned reference components.

