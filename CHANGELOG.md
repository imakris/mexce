# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- Opt-in authenticated protected-expression encoding and runtime loading on
  x64 through libsodium 1.0.22.
- The `mexce::protected` installed target and protected Conan/vcpkg package
  variants while keeping the ordinary target dependency-free.
- The opt-in `mexce_protect` issuer utility with owner-only raw-key output,
  no-overwrite publication, partial-state detection, and Windows/Linux tests.
- Protected usage, threat-model, issuer, example, and third-party notice
  documentation.

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
