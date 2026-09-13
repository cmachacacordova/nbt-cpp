# Typed zero-copy views

A tokenized document can be queried without building an owning `Tag` tree:

```cpp
auto indexed = nbt::tokenize(buffer);
auto health = indexed.get<nbt::Type::Short>("Health");
std::int16_t value = health.value();
```

`get<T>` throws `nbt::Error` when the direct child is absent or has a different type. `find<T>` is the optional alternative:

```cpp
if (auto health = indexed.find<nbt::Type::Short>("Health")) {
  use(health->value());
}
```

Nested compounds and paths are supported without allocations:

```cpp
auto nested = indexed.get<nbt::Type::Compound>("nested");
auto value = nested.get<nbt::Type::String>("value").value();
auto same = indexed.getPath<nbt::Type::String>("root.nested.value").value();
```

Lists are accessed by position and checked element type:

```cpp
auto list = indexed.get<nbt::Type::List>("values");
auto second = list.at<nbt::Type::Int>(1).value();
```

Strings return `std::string_view`; byte arrays return `std::span<const Byte>`. Int and long arrays return lazy endian-decoding views, so only accessed elements are converted:

```cpp
auto ints = indexed.get<nbt::Type::IntArray>("values").value();
std::int32_t first = ints[0];
```

Scalar values are decoded and returned by value. Views allocate no memory and copy no variable-length payload. They contain non-owning spans and become invalid when the source buffer or token storage is destroyed, reallocated or mutated incompatibly.
