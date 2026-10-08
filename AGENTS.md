# Agent instructions

## Project

- `nbt-cpp` is a C++23 header-only Java Edition NBT codec.
- The core public headers are `include/nbt/nbt.h` and `include/nbt/buffer.h`.
- `nbt::Nbt` is an alias for `nbt::NbtParser<nbt::Buffer>`; `NbtParser<nbt::Buffer>` also supports a compatible custom buffer type.
- The core codec has no required third-party dependencies.
- `include/nbt/utilities.h` provides optional SNBT, filesystem and compression helpers through `nbt::NbtUtilities`; it requires ZLIB.
- CMake exports `nbt::nbt` for the core and `nbt::utilities` for optional helpers. Preserve `find_package(nbt-cpp CONFIG REQUIRED)` compatibility.
- Keep text files LF-only and use LLVM formatting conventions.
- Use lowerCamelCase for functions, methods, fields, parameters and local variables; use PascalCase for types and enum members.

## Public API

- Namespace-level types: `nbt::Type`, `nbt::Status`, `nbt::Options`, `nbt::Exception`, `nbt::Tag`, `nbt::Buffer` and `nbt::Nbt`.
- Parse borrowed bytes without copying: `Nbt::parse(std::span<const std::byte>, options)`.
- Parse a contiguous container by copying it into the internal buffer: `Nbt::parse(container, options)`.
- Accumulate owned fragments with `document.append(chunk)`; there is no separate `feed` API. A default-constructed document treats appended bytes as unnamed. Only `parse`/`parseAtMost` configure named input through `Options::named`, and subsequent `append` calls preserve that mode.
- Inspect state with `status()`, `valid()`, `ownsBytes()`, `bytes()` and `clear()`.
- Obtain a lazy root view with `document.root()` (`nbt::TagView`, alias of `nbt::Nbt::TagView`).
- Decode an owning tree with `document.materialize()` or by converting a view via `TagView::operator Tag()`.
- Encode to a new buffer with `document.encode(named)` or append encoded data to an existing `nbt::Buffer` with `document.encode(output, named)`.
- `document.bytes()` exposes the current internal or borrowed bytes.
- `encode(named)` and `parse`/`append` with `Options::named` control the root name; `nbt::Source` no longer exists.

## Parsing and validation behavior

- Opening binary NBT validates and indexes the complete structure without decoding values into an owning tree.
- `Status::Empty` is the default state; empty input and truncated input produce `Status::NeedMoreData`; successful validation produces `Status::Complete`.
- Malformed input throws `nbt::Exception`; inspect `Exception::offset()` for the reported byte offset.
- `Options` limits are `maxDepth`, `maxContainerElements`, `maxTotalNodes`, `maxInputBytes` and `named`.
- Trailing bytes beyond the validated root are accepted and retained in `document.bytes()`; `document.encode()` writes only the valid NBT portion.
- File format is the default: one named root tag (`named = true`).
- Network/unnamed format is selected with `named = false` on `parse`/`append`/`encode`; any root type is accepted.
- `encode(false)` excludes the root name even if the input carried one; `encode(true)` emits the stored (possibly empty) name.
- With `NBT_STRICT_MODE` defined the encoder assumes well-formed input (exceptions or UB on violations); without it, invalid values are ignored during encoding — the size computation mirrors the same filtering.
- `append` copies each fragment into `Buffer`, revalidates the accumulated input and owns the resulting bytes.
- `parse(span)` borrows the caller's bytes and does not extend their lifetime; documents must not outlive the source they reference. Materialized `Tag` trees are owning and independent.
- The structural index stores offsets and node links, not pointers into individual values.
- Numeric values are encoded and decoded big-endian.

## Lazy views

- `Nbt::TagView` is the single non-owning view type over the validated bytes: `type()`, `elementType()`, `name()`, `size()`, `empty()`, `child(index)`, `operator[]`, `find(name)`, `begin()`/`end()`, `as<Type>()` and `operator Tag()`.
- `as<Type>()` decodes scalars to values, `Type::String` returns `std::string_view`, arrays return `std::span<const std::byte>` over the raw big-endian element bytes, and `Type::List`/`Type::Compound` return the same `TagView` (there are no separate view types).
- Views must not outlive the document; `operator Tag()` materializes an owning subtree.
- Documents built from a `Tag` have no bytes to view: `root()` throws for them; `materialize()` still works.

## Owning values

- `nbt::Tag` owns names and payloads and exposes `name()`, `type()`, `elementType()`, `payload()`, `size()`, `find(name)` and `depth()`.
- Its payload variant contains scalar types, `std::string`, `std::vector<Tag>` (`Tag::Container`), `std::vector<std::int8_t>`, `std::vector<std::int32_t>` and `std::vector<std::int64_t>`.
- `find` is a linear lookup over the children of a List or Compound payload and returns `const Tag*`.
- `nbt::tag_literals` provides numeric/value literals (`_tb`, `_ts`, `_ti`, `_tl`, `_tf`, `_td`), string literals (`_tgs`/`_ts`) and `name | value` helpers; prefer `std::string` or `std::string_literals` for names and string payloads when a normal string constructor is available.

## Optional utilities

- `NbtUtilities::parseSnbt(string, options)` returns an owning `Tag`.
- `NbtUtilities::toSnbt(tag, pretty)` serializes an owning tag.
- `NbtUtilities::parseFile(path, compression, options)` reads an NBT file, optionally inflates gzip or zlib data, then validates it.
- `NbtUtilities::saveFile(path, document, compression, named, level)` encodes and optionally compresses a document. `Compression::Auto` is invalid for output.
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
- CTest discovers individual GoogleTest cases with prefixes `core.` and, with utilities enabled, `utilities.`.
- Tests use GoogleTest with `gtest_discover_tests`; keep the public-behavior focus from `docs/TESTING.md` when expanding them. GoogleTest is test-only and is not linked into the core library target.

## Tag construction and named values

- `Tag` constructors accepting a name constrain the name with `std::is_constructible_v<std::string, Name &&>` and materialize it directly as `std::string`.
- `nbt::tag_literals::operator|` accepts string-like names and values convertible to `Tag`; preserve the dedicated `Tag&` overload so naming an existing tag returns the same reference.

## Testing policy and compatibility

- Read `docs/TESTING.md` before adding tests or fixtures; it contains the durable testing policy and roadmap.
- Test public behavior through core and utilities suites; do not use examples as tests or register example executables with CTest.
- Java Edition big-endian NBT is supported. Bedrock little-endian, Bedrock network/VarInt and private external formats are out of scope unless a separate implementation is added.
- Keep core tests independent of ZLIB and preserve builds with `NBT_CPP_BUILD_UTILITIES` both ON and OFF.
- Cover named and unnamed root formats, borrowed/owned/incremental input, materialization, round-trips, malformed/truncated input and resource limits.
- For canonical values include scalar limits, signed zero/non-finite floating-point cases where supported, empty and boundary-sized arrays/strings, nested lists/compounds and embedded NUL strings.
- Use independent golden vectors for exact bytes, and clean up temporary files created by filesystem tests.
- Verify Debug and Release, strict warnings, package consumption through `find_package(nbt-cpp CONFIG REQUIRED)` where relevant, and `git diff --check`.

## Agent implementation notes

- `Node` stores byte offsets (`begin`, `end`, `payload`) and child/sibling links. Tag names are not kept separately; they are reconstructed from the input bytes following the Java Edition NBT layout. For a **named** tag the on-wire layout is `[type:1][name_length:2][name:N]`, so the name bytes lie between `begin + 3` and `payload`. For **unnamed** tags — list elements and unnamed roots — there is no name header, so the payload follows the type byte directly. `NbtParser::nodeName()` guards that case and is shared by `encode()`, `materialize()` and `TagView::name()`.
- The index supports incremental reuse across `append` revalidations: `Node::end`/`payload` stay at `Node::END` while incomplete, `beginNode` replays nodes in preorder through `next_node_`, and a completed node is skipped via `end`/`nextSibling`. `parseNode` centralizes this: `named` controls the root/compound-child name header and `declared` supplies the list element type (no per-element type byte on the wire). Never hold a `Node&` across a call that can push into `nodes_` — the vector may reallocate.
- The byte-comparison helper in the test files (`equalBytes`) is kept as a local helper because `nbt::Buffer`, `std::span`, `std::array` and `std::vector` do not share a single `operator==`. Do not replace it with plain `EXPECT_EQ` unless an explicit common container is created first. Prefer `EXPECT_PRED_FORMAT2` if better failure diagnostics are needed.
