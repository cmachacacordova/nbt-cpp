# nbt-cpp

A high-performance C++23 header-only codec for Java Edition NBT. It validates structure eagerly and reads values lazily.

## Integration

```cmake
find_package(nbt-cpp CONFIG REQUIRED)
target_link_libraries(application PRIVATE nbt::nbt)
```

```cpp
#include <nbt/nbt.h>
```

## Borrowed input

```cpp
nbt::Nbt document;
auto status = document.borrow(packetBytes);
if (status == nbt::Nbt::Status::Complete) {
  auto health = document.root().find("health").asInt32();
}
```

The caller must keep borrowed bytes alive while the document or any view is used.

## Owned input

```cpp
auto bytes = std::make_unique<std::byte[]>(size);
nbt::Nbt document;
document.take(std::move(bytes), size);
```

## Incremental input

Replace a borrowed view when the same underlying message grows:

```cpp
document.borrow(partial);
document.replaceBorrowed(completeMessage);
```

Accumulate independent network fragments:

```cpp
document.feed(firstChunk);
document.feed(secondChunk);
```

Truncation returns `Nbt::Status::NeedMoreData`. Malformed input throws `nbt::Error`.

## Lazy access

```cpp
auto root = document.root();
auto player = root.find("player");
auto health = player.find("health").asInt32();
```

Views contain a document reference and node index. Names, strings and scalar values are decoded only when requested. `materialize()` explicitly creates an owning value tree.

## Encoding

```cpp
auto root = nbt::Nbt::compound("root", {
    nbt::Nbt::int32("health", 20),
    nbt::Nbt::string("name", "Alex")
});

auto bytes = nbt::Nbt::encode(root);
```

An unchanged lazy document can reproduce its validated encoded range without materializing values:

```cpp
auto copy = document.encode();
```

## Formats

```cpp
nbt::Nbt::Options options;
options.format = nbt::Nbt::Format::Network;
document.borrow(packet, options);
```

`Format::File` uses a named root. `Format::Network` requires an unnamed compound root.

## Optional utilities

SNBT, filesystem access, gzip and zlib live in a separate header and CMake target:

```cmake
target_link_libraries(application PRIVATE nbt::utilities)
```

```cpp
#include <nbt/utilities.h>

auto value = nbt::NbtUtilities::parseSnbt("{health:20}");
auto text = nbt::NbtUtilities::toSnbt(value, true);

auto document = nbt::NbtUtilities::load("level.dat");
nbt::NbtUtilities::save("copy.dat", document,
                        nbt::NbtUtilities::Compression::Gzip);
```

The core `nbt::nbt` target remains independent from zlib.
