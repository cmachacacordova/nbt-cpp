# Agent instructions

## Project

- `nbt-cpp` is a C++23 header-only Java Edition NBT codec.
- The core public headers are `include/nbt/nbt.h` and `include/nbt/buffer.h`.
- `nbt::Nbt` is an alias for `nbt::NbtParser<nbt::Buffer>`; `NbtParser<BufferT>` also supports a compatible custom buffer type.
- The core codec has no required third-party dependencies.
- `include/nbt/utilities.h` provides optional SNBT, filesystem and compression helpers through `nbt::NbtUtilities`; it requires ZLIB.
- CMake exports `nbt::nbt` for the core and `nbt::utilities` for optional helpers. Preserve `find_package(nbt-cpp CONFIG REQUIRED)` compatibility.
- Keep text files LF-only and use LLVM formatting conventions.
- Use lowerCamelCase for functions, methods, fields, parameters and local variables; use PascalCase for types and enum members.

## Public API

- Namespace-level types: `nbt::Type`, `nbt::Source`, `nbt::Status`, `nbt::Options`, `nbt::Error`, `nbt::Tag`, `nbt::Buffer` and `nbt::Nbt`.
- Parse borrowed bytes without copying: `Nbt::parse(std::span<const std::byte>, options)`.
- Parse a contiguous container by copying it into the internal buffer: `Nbt::parse(container, options)`.
- Accumulate owned fragments with `document.append(chunk, options)`; there is no separate `feed` API.
- Inspect state with `status()`, `complete()`, `ownsBytes()`, `bytes()` and `clear()`.
- Obtain a lazy root view with `document.root()`.
- Materialize an owning tree with `document.materialize()` or `view.materialize()`.
- Encode to a new buffer with `document.encode(source)` or append encoded data to an existing `BufferT` with `document.encode(output, source)`.
- `document.bytes()` exposes the current internal or borrowed bytes. There is no `encodeView()` API.
- `nbt::NbtView` aliases `nbt::Nbt::View`.

## Parsing and validation behavior

- Opening binary NBT validates and indexes the complete structure without decoding values into an owning tree.
- `Status::Empty` is the default state; empty input and truncated input produce `Status::NeedMoreData`; successful validation produces `Status::Complete`.
- Malformed input throws `nbt::Error`; inspect `Error::offset()` for the reported byte offset.
- `Options::requireCompleteInput` defaults to `true` and rejects trailing bytes when validation succeeds structurally.
- `Options` limits are `maxDepth`, `maxContainerElements`, `maxTotalNodes` and `maxInputBytes`.
- File format is the default: one named root tag.
- Network format is selected with `options.format = nbt::Source::Network` and requires an unnamed `TAG_Compound` root.
- `append` copies each fragment into `Buffer`, revalidates the accumulated input and owns the resulting bytes.
- `parse(span)` borrows the caller's bytes and does not extend their lifetime; documents and views must not outlive or move away from the source/document they reference.
- The structural index stores offsets and node links, not pointers into individual values.
- Numeric values are encoded and decoded big-endian.

## Views and owning values

- `Nbt::View` exposes `type()`, `elementType()`, `name()`, `size()`, `empty()`, `operator[]`, `child(index)`, `find(name)`, `begin()`/`end()`, `as<Type>()` and `materialize()`.
- `as<Type::ByteArray>()` returns `std::span<const std::byte>` into the source buffer.
- `as<Type::IntArray>()` and `as<Type::LongArray>()` return lazy big-endian array views with `size()`, indexing and iteration.
- `as<Type::List>()` and `as<Type::Compound>()` return a container-capable `View`.
- `find` and indexed `child` traversal follow sibling links and are O(index) per access; avoid repeated indexed lookup in hot loops when range iteration is sufficient.
- `nbt::Tag` owns names and payloads. Its payload variant contains scalar types, `std::string`, `std::vector<Tag>`, `std::vector<std::int8_t>`, `std::vector<std::int32_t>` and `std::vector<std::int64_t>`.
- `nbt::tag_literals` provides numeric/value literals (`_tb`, `_ts`, `_ti`, `_tl`, `_tf`, `_td`), string literals (`_tgs`/`_ts`) and `name | value` helpers; prefer `std::string` or `std::string_literals` for names and string payloads when a normal string constructor is available.

## Optional utilities

- `NbtUtilities::parseSnbt(string, options)` returns an owning `Tag`.
- `NbtUtilities::toSnbt(tag, pretty)` serializes an owning tag.
- `NbtUtilities::load(path, compression, options)` reads an NBT file, optionally inflates gzip or zlib data, then validates it.
- `NbtUtilities::save(path, document, compression, source, level)` encodes and optionally compresses a document. `Compression::Auto` is invalid for output.
- The utilities header includes `<zlib.h>` and must remain optional from the core target.

## Build and verify

```sh
cmake -S . -B build-vcpkg -DCMAKE_TOOLCHAIN_FILE=C:/Workspace/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-vcpkg --config Debug
ctest --test-dir build-vcpkg -C Debug --output-on-failure
cmake --build build-vcpkg --config Release
ctest --test-dir build-vcpkg -C Release --output-on-failure
```

Run `git diff --check` before completing changes. Do not modify unrelated working-tree files; in particular, preserve existing changes and untracked files unless the user explicitly asks otherwise.

## Tests and examples

- `tests/nbt_tests.cpp` contains core codec tests and does not include optional ZLIB utilities.
- `tests/utilities_tests.cpp` contains SNBT, filesystem and compression tests and is built only when `NBT_CPP_BUILD_UTILITIES` is enabled.
- Examples under `examples/` are standalone usage programs. They must be built when `NBT_CPP_BUILD_EXAMPLES` is enabled, but must not be registered as CTest tests.
- CTest names are `nbt-cpp-core-tests` and, with utilities enabled, `nbt-cpp-utilities-tests`.
- The dependency-free test harness currently uses executable exit status rather than GoogleTest; keep the public-behavior focus from `docs/TESTING.md` when expanding it.

## Tag construction and named values

- `Tag` constructors accepting a name constrain the name with `std::is_constructible_v<std::string, Name &&>` and materialize it directly as `std::string`.
- `nbt::tag_literals::operator|` accepts string-like names and values convertible to `Tag`; preserve the dedicated `Tag&` overload so naming an existing tag returns the same reference.

## Testing policy and compatibility

- Read `docs/TESTING.md` before adding tests or fixtures; it contains the durable testing policy and roadmap.
- Test public behavior through core and utilities suites; do not use examples as tests or register example executables with CTest.
- Java Edition big-endian NBT is supported. Bedrock little-endian, Bedrock network/VarInt and private external formats are out of scope unless a separate implementation is added.
- Keep core tests independent of ZLIB and preserve builds with `NBT_CPP_BUILD_UTILITIES` both ON and OFF.
- Cover File and Network formats, borrowed/owned/incremental input, lazy views, materialization, round-trips, malformed/truncated input and resource limits.
- For canonical values include scalar limits, signed zero/non-finite floating-point cases where supported, empty and boundary-sized arrays/strings, nested lists/compounds and embedded NUL strings.
- Use independent golden vectors for exact bytes, and clean up temporary files created by filesystem tests.
- Verify Debug and Release, strict warnings, package consumption through `find_package(nbt-cpp CONFIG REQUIRED)` where relevant, and `git diff --check`.
