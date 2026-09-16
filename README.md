# nbt-cpp

Modern C++ library for Java Edition NBT. Binary input and output use the standard `std::istream` and `std::ostream` interfaces, including file, memory, network, and custom stream buffers.

## Features

- Owning NBT tree model and builder.
- Binary File NBT and Java 1.20.2+ Network NBT.
- Stream-based parsing, token indexing, serialization, gzip, and zlib.
- Incremental parsing for seekable streams through `tryParse`.
- SNBT parsing and serialization.
- Traversal, filtering, search, cloning, and comparison utilities.
- Static/shared builds, CMake package export, and vcpkg manifest.

## Build

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Workspace/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Parse and serialize

```cpp
#include <fstream>
#include <nbt/nbt.h>

std::ifstream input("level.dat", std::ios::binary);
nbt::Tag root = nbt::parse(input);

std::ofstream output("copy.dat", std::ios::binary);
nbt::serialize(output, root);
```

For in-memory data, use standard string streams:

```cpp
std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
nbt::serialize(stream, root);
stream.seekg(0);
nbt::Tag copy = nbt::parse(stream);
```

## Compressed files

Compression is intentionally outside the NBT API. The caller supplies a stream that already exposes uncompressed bytes. For gzip or zlib files, `zstr` can provide that stream:

```cpp
zstr::ifstream input("level.dat", std::ios::binary);
nbt::Tag root = nbt::parse(input);

zstr::ofstream output("copy.dat", std::ios::binary);
nbt::serialize(output, root);
```

This keeps parsing and serialization independent from files and compression formats. Applications may use `zstr`, another compression library, a socket-backed `std::streambuf`, or ordinary standard streams.

For filesystem convenience, `load` and `save` support uncompressed, gzip, and zlib NBT files:

```cpp
nbt::Tag root = nbt::load("level.dat");
nbt::save("copy.dat", root, nbt::Compression::Gzip);
```

These are the only public APIs that handle compression; generic stream compression remains outside the library.

## Incremental input

`tryParse` restores the read position and returns `std::nullopt` when a seekable stream does not yet contain a complete document. Malformed input still throws `nbt::Error`; truncated input throws `nbt::IncompleteDataError` from `parse`.

```cpp
auto result = nbt::tryParse(stream);
if (!result) {
  // Append more data to the stream and retry.
}
```

A non-seekable stream should use `parse`; its `std::streambuf` is responsible for blocking until requested bytes are available. This is the normal model for socket-backed streams.

## Network NBT

```cpp
nbt::ParseOptions options;
options.format = nbt::BinaryFormat::Network;
nbt::Tag root = nbt::parse(input, options);
nbt::serialize(output, root, nbt::BinaryFormat::Network);
```

Network NBT requires an unnamed root compound and omits the root name field.

## Tokenization

```cpp
auto document = nbt::tokenize(input);
```

Tokens contain stream-relative offsets and structural metadata only. They do not retain a source buffer or pointer and therefore require no source identity/content validation. `encodedSize(root, format)` calculates the serialized size from a tree, while `encodedSize(document)` reads it from the indexed root token.

## Public headers

- `nbt/nbt.h` and compatibility header `nbt/nbt.hpp` — complete API.
- `nbt/type.h` — enums.
- `nbt/error.h` — exceptions.
- `nbt/tag.h` — tree model and tag factories.
- `nbt/token.h` — token and parse option types.
- `nbt/builder.h` — tree builder.
- `nbt/stream.h` — stream parsing and tokenization.

See `docs/BINARY_FORMATS.md`, `docs/TOKENS.md`, and `docs/SNBT.md` for details.
