# nbt-cpp

Modern C++20 library for Java Edition Named Binary Tag data. It combines a zero-copy token index over an input buffer with an owning tree model, direct parsing, parsing validated against tokens, binary writing, gzip/zlib support, builders, traversal, search, filtering and SNBT output.

All standard tags are supported: `TAG_End`, numeric tags, `TAG_Byte_Array`, `TAG_String`, `TAG_List`, `TAG_Compound`, `TAG_Int_Array`, and `TAG_Long_Array`. Binary data uses the Java Edition big-endian representation. Strings preserve their encoded bytes; validation or conversion of Java's modified UTF-8 is left to the caller.

## Build

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Workspace/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The library target is `nbt::nbt`; zlib is the only dependency.

## Static and shared builds

By default CMake builds a static library. Build a shared library with:

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
```

With vcpkg the linkage follows `VCPKG_LIBRARY_LINKAGE` (`static` or `dynamic`):

```sh
vcpkg install nbt-cpp:x64-windows --overlay-ports=ports        # dynamic
vcpkg install nbt-cpp:x64-windows-static --overlay-ports=ports  # static
```

Windows consumers of a shared build must place `nbt-cpp.dll` on `PATH` or next to the executable. The public API uses standard C++ types, so the library and the consumer must be built with the same C++20 ABI and MSVC runtime library to avoid ODR mismatches.

## Install with vcpkg

Use the included overlay port:

```sh
vcpkg install nbt-cpp --overlay-ports=ports
```

In manifest mode, add `nbt-cpp` to the consumer's `vcpkg.json` and configure with:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=/path/to/nbt-cpp/ports
```

Consume the installed package with:

```cmake
find_package(nbt-cpp CONFIG REQUIRED)
target_link_libraries(application PRIVATE nbt::nbt)
```

The current port is reproducibly pinned to a GitHub commit and uses Git/SSH, so private repository access follows the user's configured SSH credentials.

## Install as a CMake package

```sh
cmake -S . -B build -DNBT_CPP_BUILD_TESTS=OFF -DNBT_CPP_BUILD_EXAMPLES=OFF
cmake --build build --config Release
cmake --install build --config Release --prefix install
```

Consumers can set `CMAKE_PREFIX_PATH` to the install prefix and use the same `find_package` and target shown above.

## Parse directly or through tokens

```cpp
#include <nbt/nbt.h>

nbt::Buffer input = /* uncompressed NBT bytes */;
nbt::Tag direct = nbt::parse(input);

nbt::TokenizedDocument document = nbt::tokenize(input);
nbt::Tag checked = nbt::parse(document);
```

Tokens retain byte ranges, parent indexes, subtree boundaries, list count, and list element type without copying names or payloads. Tokenization has a dedicated scanner: it never constructs a `Tag`, string, or payload array. Compact 32-bit tokens occupy at most 24 bytes and support buffers up to 4 GiB.

`TokenizedDocument` binds the source span and token vector. The default `SourceValidation::Identity` has no full-buffer hashing cost: `parse(document)` uses the bound span, while `parse(input, document)` checks pointer and size in `O(1)`. Content hashing is available only through opt-in `SourceValidation::Content`; `SourceValidation::None` is the explicit unchecked mode.

Caller-owned token storage remains allocation-free:

```cpp
std::vector<nbt::Token> storage(4096);
nbt::TokenizedView view = nbt::tokenize(input, storage);
nbt::Tag tree = nbt::parse(view);
```

The tokenized document can also be queried without building a tree:

```cpp
auto health = document.get<nbt::Type::Short>("Health");
std::int16_t value = health.value();
auto nested = document.getPath<nbt::Type::String>("root.player.name");
```

These typed views allocate nothing. Strings and byte arrays reference the source directly, while int/long arrays decode endian values lazily.

`ParseOptions` provides depth and collection-size limits and can permit trailing protocol bytes with `requireCompleteInput = false`. For Java Edition Network NBT since 1.20.2 (protocol 764), explicitly select `BinaryFormat::Network`. This requires a root `TAG_Compound`, writes or reads its `0x0A` type byte, and omits the root name length and name entirely:

```cpp
nbt::ParseOptions options;
options.format = nbt::BinaryFormat::Network;
auto tokens = nbt::tokenize(packet_nbt, options);
nbt::Tag root = nbt::parse(packet_nbt, tokens, options);
nbt::Buffer encoded = nbt::serialize(root, nbt::BinaryFormat::Network);
```

Use the default `BinaryFormat::File` for world, player and other persisted NBT data. Pre-1.20.2 protocol NBT with an empty root name also uses `BinaryFormat::File`, because its two-byte zero name length is still present.

## Build and serialize

```cpp
nbt::Builder builder("root");
builder.add(nbt::intTag("DataVersion", 3955))
       .beginList("values", nbt::Type::String)
       .add(nbt::stringTag("", "one"))
       .add(nbt::stringTag("", "two"))
       .end();

nbt::Tag root = builder.build();
nbt::Buffer binary = nbt::serialize(root);
nbt::save("data.nbt", root, nbt::Compression::Gzip);
```

## SNBT

```cpp
nbt::Tag tree = nbt::parseSnbt(R"({name:"Steve",health:20s,pos:[1.0d,64.0d,-3.5d]})");
std::string text = nbt::toSnbt(tree, true);
```

The parser supports quoted and unquoted strings, escapes, compounds, homogeneous lists, typed byte/int/long arrays, numeric suffixes, exponents, and boolean byte values.

## Examples

Examples are built by default with `NBT_CPP_BUILD_EXAMPLES=ON`:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Workspace/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Generated executables:

- `nbt-read-gzip [path]`: loads a gzip NBT file and prints its complete SNBT representation. Without a path it uses `tests/level.dat`.
- `nbt-query-views`: demonstrates typed zero-copy queries.
- `nbt-build`: builds and serializes an NBT tree.
- `nbt-snbt`: parses SNBT, serializes it to binary and prints the restored tree.

Disable them with `-DNBT_CPP_BUILD_EXAMPLES=OFF`.

## Optimized builds

Release builds enable IPO/LTO when supported. Portable CPU code remains the default. Use `-DNBT_CPP_NATIVE_ARCH=ON` for `/arch:AVX2` on MSVC or `-march=native -mtune=native` on GCC/Clang when the resulting binary only needs to run on the build machine or a compatible CPU.

## Utilities

The public API in `include/nbt/nbt.h` includes:

- `load`, `save`, `compress`, and `decompress` for raw, gzip, and zlib data.
- `clone`, mutable/const `map`, `filter`, and `filterInPlace`.
- Predicate `find`, `findByName`, dotted `findByPath`, and indexed `at`.
- Recursive `size`, structural `equivalent`, `typeName`, and `toSnbt`.
- Factory methods for every NBT tag and a checked nested `Builder`.

Malformed lengths, unknown types, truncation, invalid list metadata, excess depth, trailing bytes, and mismatched token sources produce `nbt::Error`, including the failing input offset.

## Documentation

- [Tokenization and source validation](docs/TOKENS.md)
- [Typed zero-copy views](docs/VIEWS.md)
- [File and Network binary formats](docs/BINARY_FORMATS.md)
- [SNBT](docs/SNBT.md)
