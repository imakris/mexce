# Changelog

All notable changes to this project will be documented in this file.

## [1.2.0] - 2026-10-11

### Added

- Native Apple Silicon ARM64 JIT compilation, including scalar arithmetic,
  math calls, common subexpression elimination, and macOS executable-memory
  protection. Windows and Linux x86 backends remain supported.
- Opt-in authenticated protected-expression encoding and runtime loading on
  x86-64 and Apple Silicon through libsodium 1.0.22.
- The `mexce::protected` installed target and source package recipes with
  protected-expression features, while keeping the ordinary target
  dependency-free.
- The opt-in `mexce_protect` issuer utility with owner-only raw-key output,
  no-overwrite publication, partial-state detection, and Windows, Linux, and
  macOS support.
- Protected usage, threat-model, issuer, example, and third-party notice
  documentation.

### Changed

- Made OpenMP optional for benchmark timing; ordinary builds and tests work
  without it.
- Improved SSE2 arithmetic lowering and benchmark measurement stability.
- Split Windows, Linux, and macOS validation into separate CI workflows,
  including installed ordinary and protected consumers.

### Fixed

- Released temporary executable buffers after constant folding.
- Preserved arguments across SSE2 binary math calls and accepted the `>`
  infix operator during tokenization.
- Kept IEEE-visible cancellations out of default-mode arithmetic merging.
- Made first-use function-map initialization safe across concurrent threads.
- Avoided collisions with Qt's `emit` macro.

## [1.0.1] - 2025-10-10
### Added
- GitHub Actions workflows for build, benchmark, and coverage reporting, plus Codecov configuration and badges to track quality metrics. 
- Conan package recipe with an accompanying CMake-based test package for consumers.
- Extensive unit and benchmark test coverage, including helpers to simulate system call failures and improved timing utilities.
- New built-in comparison helpers and greater-than infix support, exposing `lt`, `le`, `gt`, `ge`, `eq`, and `ne` functions.

### Changed
- Updated packaging metadata (Conan and vcpkg) to version 1.0.1.
- Enhanced parser diagnostics for mismatched parentheses during expression compilation.
- Clarified README examples and documented the extended comparison operators.

### Fixed
- Prevented locking executable buffers with zero length to avoid undefined behavior during JIT code emission.

## [1.0.0] - 2025-09-25
### Added
- Initial public release.
