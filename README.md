# nbt-cpp

`nbt-cpp` is a C++23 header-only codec for Java Edition NBT. The binary codec validates and indexes the complete structure when input is opened, while names, scalar values, strings and arrays are decoded on demand through lightweight views.

## Requirements

- C++23 compiler.
- No required third-party dependency for the binary codec.
- GoogleTest is required only when building the test suites.
- ZLIB is required only when building the optional utilities target.

## Integration

The installed package exports the core `nbt::nbt` interface target:

```cmake
find_package(nbt-cpp CONFIG REQUIRED)
target_link_libraries(application PRIVATE nbt::nbt)
```

Include the binary codec with:

```cpp
#include <nbt/nbt.h>
```

When utilities are enabled, link the optional target as well:

```cmake
target_link_libraries(application PRIVATE nbt::utilities)
```

```cpp
#include <nbt/utilities.h>
```

CMake options:

- `NBT_CPP_BUILD_TESTS` (default `ON`)
- `NBT_CPP_BUILD_EXAMPLES` (default `ON`)
- `NBT_CPP_BUILD_UTILITIES` (default `ON`, requires ZLIB)
- `NBT_STRICT_MODE` (default `OFF`): when enabled the encoder assumes well-formed input and performs no value validation; when disabled, invalid values are ignored during encoding.

## Parsing input

The public `nbt::Nbt` type is an alias for `nbt::NbtParser<nbt::Buffer>`.

### Borrowed, zero-copy input

Parsing a `std::span<const std::byte>` refers to the caller-owned bytes without copying them:

```cpp
std::span<const std::byte> packetBytes = /* complete NBT message */;
nbt::Nbt document = nbt::Nbt::parse(packetBytes);

if (document.status() == nbt::Status::Complete) {
  const auto health = document.root().find("health").as<nbt::Type::Int>();
}
```

The source bytes must remain alive and unchanged while the document or any view is used. `document.ownsBytes()` is `false` for this input mode.

### Owned input

Parsing a contiguous container copies its bytes into the parser's internal `nbt::Buffer`:

```cpp
std::vector<std::byte> bytes = /* NBT message */;
nbt::Nbt document = nbt::Nbt::parse(bytes);
```

The document owns the copied bytes, so the source container may be released after parsing. `document.ownsBytes()` reports whether the parser has an internal buffer.

### Incremental input

Use `append` to accumulate independent fragments. Each call copies the fragment and validates the accumulated bytes. Revalidation reuses the structural index: nodes already validated are skipped through their sibling links, so only the truncated frontier is re-examined on each call.

```cpp
nbt::Nbt document;
document.append(firstChunk);
if (document.status() == nbt::Status::NeedMoreData) {
  document.append(secondChunk);
}

if (document.valid()) {
  const auto root = document.root();
}
```

An empty parser has `Status::Empty`. Truncated input has `Status::NeedMoreData`; malformed input throws `nbt::Exception`, whose `offset()` identifies the byte position when available. `clear()` resets the document to `Status::Empty`.

## Named and unnamed root tags

By default a document expects and emits a **named** root tag (file format). An unnamed root (network format) is selected with the `named` flag: set `Options::named` to `false` when parsing, and pass `false` to `encode`:

```cpp
nbt::Options options;
options.named = false;
nbt::Nbt document = nbt::Nbt::parse(packetBytes, options);
```

```cpp
auto networkBytes = document.encode(false);
```

`encode(false)` excludes the root name even when the source carried one, and `encode(true)` emits the stored (possibly empty) name. The same `named` flag applies to `append` when accumulating chunks. Any NBT type is accepted as the root.

The parser applies configurable safety limits through `Options`: maximum depth, container elements, total nodes and input bytes. Trailing bytes beyond the validated root are accepted and retained in `document.bytes()`; `document.encode()` writes only the valid NBT portion.

## Lazy views and materialization

`document.root()` returns an `nbt::Nbt::View`. A view is valid only while its originating document remains alive, unmoved and attached to the same input.

```cpp
auto root = document.root();
auto player = root.find("player");
auto health = player.find("health").as<nbt::Type::Int>();
```

`View` supports:

- `type()`, `elementType()` and `name()` metadata access.
- `as<Type>()` for scalar values, strings, byte arrays, integer-array views and container views.
- `size()`, `empty()`, `operator[]`, `child(index)`, `find(name)` and range iteration for lists and compounds.
- `materialize()` to create an owning `nbt::Tag` subtree.

Integer arrays are exposed as lazy `IntArrayView` and `LongArrayView` values. Byte arrays are exposed as `std::span<const std::byte>`. Call `document.materialize()` to create an owning tree for the complete document.

## Encoding

`nbt::Tag` is the owning representation. It supports NBT scalar values, strings, lists, compounds and byte/int/long arrays:

```cpp
using namespace nbt::tag_literals;
using namespace std::string_literals;

nbt::Tag root("root", nbt::Tag::Container{
    "health"s | 20_ti,
    "name"s | "Alex"s});

nbt::Nbt document(root);
nbt::Buffer bytes = document.encode();
```

An unchanged parsed document can be copied to a new buffer without materializing its values. Encoding into an existing buffer is also supported:

```cpp
nbt::Buffer output;
document.encode(output);
```

`document.bytes()` returns a span over the parser's current internal or borrowed bytes. `encode(named)` can switch between named and unnamed output regardless of how the input was parsed.

## Optional utilities

`nbt::NbtUtilities` provides SNBT conversion, filesystem I/O and ZLIB compression helpers:

```cpp
using Utilities = nbt::NbtUtilities;

auto value = Utilities::parseSnbt("{health:20}");
auto text = Utilities::toSnbt(value, true);

auto document = Utilities::parseFile("level.dat");
Utilities::saveFile("copy.dat", document, Utilities::Compression::Gzip);
```

Supported compression modes are `None`, `Gzip`, `Zlib` and `Auto`. `Auto` detects gzip or zlib input when loading and is invalid for output. The utilities header requires ZLIB.

## Examples and tests

Configure with CMake, then build and run the tests:

```sh
cmake -S . -B build -DNBT_CPP_BUILD_UTILITIES=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

CTest runs the core codec suite and, when utilities are enabled, a separate utilities suite. The programs under `examples/` are usage demonstrations and are built by CMake, but are not registered as tests. They cover lazy parsing, fragmented buffers, owning-tree construction, network NBT, view queries, gzip loading and SNBT.

The current test sources are organized as follows:

- `tests/nbt_tests.cpp`: core construction, parsing, views, encoding, file/network formats and malformed-input tests.
- `tests/utilities_tests.cpp`: SNBT and filesystem/compression round-trip tests.
- `examples/`: standalone usage programs that are intentionally independent from CTest.

The testing policy, Java-versus-Bedrock compatibility boundary, canonical value matrix and roadmap are documented in [`docs/TESTING.md`](docs/TESTING.md).
