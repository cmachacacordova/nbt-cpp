# Testing and compatibility

## Scope

`nbt-cpp` implements binary NBT for Minecraft Java Edition. Java NBT uses big-endian byte order. The root tag may be named (file format) or unnamed (network format); the `named` flag in `nbt::Options` (parse/`append`) and in `encode` selects between them. Any NBT type is accepted as the root.

The library does not implement little-endian Bedrock NBT, Bedrock network/VarInt NBT, or private formats used by external tools. Those formats must not be treated as compatible or used as positive fixtures.

Tests validate observable behavior through the public API rather than implementation details. Current and planned coverage includes:

- `Tag` construction, literals, string-like names, and values convertible to `Tag`.
- Encoding, parsing, materialization, and round trips for every NBT type.
- Named and unnamed root tags (file and network formats), including re-encoding with the opposite flag.
- Borrowed input, owned input, and incremental accumulation with `append`.
- Lazy views, iteration, arrays, and document/view lifetimes.
- SNBT, filesystem access, and None, Gzip, and Zlib compression.
- Truncated and malformed input, error offsets, and resource limits.
- Canonical fixtures, interoperability, real Minecraft data, fuzzing, and performance as they are added.

## Current organization

- `tests/nbt_tests.cpp`: core codec tests without ZLIB.
- `tests/utilities_tests.cpp`: SNBT, filesystem, and compression tests; built only with `NBT_CPP_BUILD_UTILITIES=ON`.
- `examples/`: standalone programs that demonstrate library usage. They are built but are not registered as CTest tests.
- Discovered core tests use the `core.` prefix and the `core;codec` labels.
- Discovered utility tests use the `utilities.` prefix and the `utilities;io` labels when utilities are enabled.

The test suites use GoogleTest and `gtest_discover_tests`. GoogleTest is a test-only dependency and is not linked by the core `nbt::nbt` target. ZLIB remains optional and is required only by the utilities target and test suite.

## Value matrix

Each applicable operation should test at least:

- `Byte`, `Short`, `Int`, and `Long`: limits, negative values, zero, and positive values.
- `Float` and `Double`: signed zero, extreme finite values, subnormal values, infinity, and NaN when supported by the operation.
- `String`: empty, ASCII, UTF-8, embedded NUL, and byte-length boundaries.
- `ByteArray`, `IntArray`, and `LongArray`: empty, one element, boundary values, and larger sizes.
- `List` and `Compound`: empty, one element, nesting, many children, duplicate names, and valid types.
- `End`: a valid terminator and illegal appearances as a normal tag.

Floating-point values should be compared by representation when the contract requires bit preservation, including the sign of zero and NaN handling.

## Rules for new tests

1. Test public, observable behavior rather than private fields or functions.
2. Prefer integration paths such as `Tag -> encode -> parse -> View -> materialize` and `bytes -> parse -> encode`.
3. Keep core and utilities tests separate.
4. Use independent golden vectors when checking exact bytes.
5. Test every relevant cut point for truncated input and distinguish `NeedMoreData` from malformed-input errors.
6. For limits, test `limit - 1`, `limit`, and `limit + 1` where applicable.
7. Tests that create temporary files must remove them and must not depend on a repository directory.
8. Do not use examples as substitutes for tests or register example executables with CTest merely to verify that they compile.

## Infrastructure and compatibility

The minimum configurations to verify are:

- Utilities ON and OFF.
- Debug and Release.
- MSVC with `/W4 /permissive-`; equivalent strict warnings on other compilers.
- `git diff --check`.

Installed-package compatibility should also be tested with a consumer that calls `find_package(nbt-cpp CONFIG REQUIRED)` and links `nbt::nbt`. Repeat the check with `nbt::utilities` when ZLIB is enabled.

## Open decisions

Before adding new conformance vectors, explicitly document:

- The resolved `named` flag policy: any input is stored as a named tag, and `encode` chooses whether to emit the name.
- The `NBT_STRICT_MODE` boundary: strict builds assume well-formed input (exceptions or UB on violations); non-strict builds ignore invalid values during encoding. Tests should cover both when the build defines the macro.
- The current absence of `encodeView()`; do not document or test an API that does not exist.
- The policy for invalid UTF-8.
- Preservation of NaN and non-finite values in SNBT.
- Concatenated Gzip members.
- The semantics of `append` after `Status::Complete` are resolved: appended bytes become retained trailing input and the already validated root is not re-examined.
- The trailing-byte policy: trailing bytes are accepted and retained in `document.bytes()`; `encode()` emits only the valid NBT portion.
- The explicit Bedrock compatibility boundary.

## Roadmap

1. Consolidate helpers and canonical vectors without introducing mandatory dependencies.
2. Complete conformance coverage for every type, File/Network formats, borrowed/owned input, and round trips.
3. Expand streaming, view, error, and limit coverage.
4. Complete SNBT and I/O coverage, including filesystem and compression failures.
5. Add real Minecraft fixtures and a reproducible interoperability corpus.
6. Add fuzz smoke tests, sanitizers, and benchmarks outside strict functional CI thresholds.
