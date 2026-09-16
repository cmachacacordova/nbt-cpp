# Agent instructions

## Project

- `nbt-cpp` is a C++ Java Edition NBT library.
- Public API: `include/nbt/nbt.h` (and the backward-compatible `include/nbt/nbt.hpp`).
- Implementation: `src/nbt.cpp`.
- Tests: `tests/nbt_tests.cpp`.
- Public headers are split by concern: `type.h`, `tag.h`, `token.h`, `builder.h`, `error.h`, `stream.h`.
- Stream parsing/serialization is compression-agnostic; filesystem convenience APIs use `zstr`, with zlib supplied transitively.
- The CMake package exports `nbt::nbt`; keep `find_package(nbt-cpp CONFIG REQUIRED)` working.
- The overlay port is under `ports/nbt-cpp` and is validated through `VCPKG_OVERLAY_PORTS`.
- Keep all text files LF-only.
- Use lowerCamelCase for functions, methods, fields, parameters and local variables; use PascalCase for types and enum members.

## Required behavior

- Binary parsing, tokenization and serialization use standard `std::istream` and `std::ostream`; `load`/`save` additionally support uncompressed, gzip and zlib files.
- Do not introduce custom byte buffers, source spans, source fingerprints, or non-owning pointers to source data.
- Tokens contain stream-relative offsets and structural metadata only; keep them at no more than 24 bytes.
- `TokenizedDocument` owns only its token vector; do not add source data or source pointers to it.
- `BinaryFormat::Network` implements Java 1.20.2+/protocol 764 Network NBT: root type byte `0x0A`, no root name length/name, and a mandatory root compound.
- `BinaryFormat::File` retains the ordinary named-root representation.
- Preserve binary round-trip and SNBT round-trip tests when changing the data model.
- `IncompleteDataError` (derived from `Error`) is thrown for truncated input; malformed input still throws `Error`.
- `tryParse` preserves context by restoring a seekable stream position when more data is required; non-seekable stream buffers should block until data arrives.
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
