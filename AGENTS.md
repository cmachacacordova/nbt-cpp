# Agent instructions

## Project

- `nbt-cpp` is a C++20 Java Edition NBT library.
- Public API: `include/nbt/nbt.h` (and the backward-compatible `include/nbt/nbt.hpp`).
- Implementation: `src/nbt.cpp`.
- Tests: `tests/nbt_tests.cpp`.
- Public headers are split by concern: `buffer.h`, `type.h`, `tag.h`, `token.h`, `builder.h`, `error.h`, `stream.h`.
- zlib is supplied through the `vcpkg.json` manifest.
- The CMake package exports `nbt::nbt`; keep `find_package(nbt-cpp CONFIG REQUIRED)` working.
- The overlay port is under `ports/nbt-cpp` and is validated through `VCPKG_OVERLAY_PORTS`.
- Keep all text files LF-only.
- Use lowerCamelCase for functions, methods, fields, parameters and local variables; use PascalCase for types and enum members.

## Required behavior

- Preserve both owning tree parsing and zero-copy binary tokenization.
- Tokenization must not construct `Tag` objects or copy names/payloads.
- The caller-owned token overload must remain allocation-free.
- `TokenizedDocument` and `TokenizedView` bind tokens to a non-owning source span. Keep identity validation `O(1)` by default and do not add full-buffer work to the default hot path.
- Content fingerprints belong once in document metadata, never in each `Token`, and are generated only for opt-in `SourceValidation::Content`.
- Keep `SourceValidation::None` as the explicit unchecked mode and tokens at no more than 24 bytes.
- Typed `TagView` queries must remain allocation-free. Strings/byte arrays are direct views; int/long arrays decode lazily without materializing arrays.
- `BinaryFormat::Network` implements Java 1.20.2+/protocol 764 Network NBT: root type byte `0x0A`, no root name length/name, and a mandatory root compound.
- `BinaryFormat::File` retains the ordinary named-root representation.
- Preserve binary round-trip and SNBT round-trip tests when changing the data model.
- `IncompleteDataError` (derived from `Error`) is thrown for truncated input; malformed input still throws `Error`.
- `StreamParser` supports incremental parsing from non-contiguous buffers; it keeps unconsumed bytes and allows trailing data by default.
- Do not enable native CPU instructions by default; `NBT_CPP_NATIVE_ARCH` is opt-in.

## Build and verify

On this workspace:

```sh
cmake -S . -B build-vcpkg -DCMAKE_TOOLCHAIN_FILE=C:/Workspace/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-vcpkg --config Debug
ctest --test-dir build-vcpkg -C Debug --output-on-failure
cmake --build build-vcpkg --config Release
ctest --test-dir build-vcpkg -C Release --output-on-failure
```

Static and shared linkage are both supported; use `BUILD_SHARED_LIBS` for CMake and `VCPKG_LIBRARY_LINKAGE` for vcpkg. Windows shared builds require matching CRT/ABI between the library and the consumer.

Run `git diff --check` before completing changes.
