# Binary formats

## File NBT

File NBT contains a root type, root name length/name, and payload.

```cpp
nbt::Tag root = nbt::parse(input);
nbt::serialize(output, root);
```

## Network NBT

Java 1.20.2+/protocol 764 Network NBT begins with `TAG_Compound` (`0x0A`), omits the root name, and requires a compound root.

```cpp
nbt::ParseOptions options;
options.format = nbt::BinaryFormat::Network;
auto root = nbt::parse(input, options);
nbt::serialize(output, root, nbt::BinaryFormat::Network);
```

Both formats operate directly on `std::istream` and `std::ostream`.
