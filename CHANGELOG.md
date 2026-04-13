# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
### Added
- Portable `page_size()` helper and `round_up_to_page()` for correct allocation
  sizing on systems with non-4 KB pages.
- `scoped_executable_buffer` RAII wrapper for the JIT code buffer, used to
  install new programs atomically.
- Support for macOS (Intel), FreeBSD, and the other BSDs alongside Windows and
  Linux. Unsupported platforms now `#error` out with a clear message rather
  than silently producing broken code.
- Parser guards against pathological input: `MEXCE_MAX_EXPRESSION_LENGTH`
  (default 1 MiB) and `MEXCE_MAX_NESTING_DEPTH` (default 256) now produce a
  clear `mexce_parsing_exception` instead of unbounded memory use.
- `mexce_parsing_exception::position()` accessor for the offset of the
  offending character.
- CI workflow that builds under `-fsanitize=address,undefined` with both GCC
  and Clang and runs the full unit test suite.

### Changed
- `evaluator::evaluate(const std::string&)` now copies the parent evaluator's
  options (fast_math, prefer_x87, enable_cse) into the temporary evaluator it
  creates, so single-shot evaluations respect the configured behavior.
- `enable_cse()` now forces the x87 backend on the next compilation. CSE is
  x87-only, and silently dropping the setting on x86-64 was the historical
  footgun.
- `set_expression()` now uses an exception-safety guard: if parsing or code
  generation throws, `evaluate_fptr` is deliberately invalidated so a
  subsequent `evaluate()` raises a clear `std::runtime_error` instead of
  dereferencing dangling intermediate-constant pointers. Successful
  compilation installs the new program atomically and only then frees the
  previous one.
- Numeric literals are parsed with `std::strtod` and validated for range;
  previously `atof` silently turned e.g. `1e+400` into `+Inf`.

### Removed
- The `options::use_libm_sin`/`cos`/`tan`/`exp`/`log`/`log10`/`log2`/`logb`/
  `ylog2`/`generic_pow` fields and `options::set_use_libm()` convenience.
  These were never read at run time (the real decision is compile-time via
  `MEXCE_USE_LIBM_*` macros) and had become a misleading API.

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
