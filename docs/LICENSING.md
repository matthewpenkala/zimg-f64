# Licensing and attribution

## Repository

The upstream `COPYING` file is preserved verbatim. zimg is distributed under
the WTFPL, version 2. Existing source notices and copyright statements remain
intact.

New fork-owned C++, Python, CMake, workflow, test, and documentation files are
offered under the same WTFPL v2 terms unless a file explicitly states
otherwise.

## Submodules

The repository does not relicense its submodules:

- graphengine is retained at its pinned commit under its own WTFPL v2
  `COPYING`;
- both GoogleTest submodules retain Google's three-clause BSD-style `LICENSE`
  text and notices.

Review those directories for their authoritative terms.

## Optional system and Python dependencies

MPFR, GMP, CMake, Ninja, compilers, FFmpeg, pytest, Hypothesis, mpmath, SymPy,
NumPy, SciPy, Ruff, and mypy are optional build/test tools. They are not
vendored or redistributed by this fork and retain their own licenses.

No platform binaries are attached to the initial release, avoiding any
runtime-bundling or binary-notice ambiguity.
