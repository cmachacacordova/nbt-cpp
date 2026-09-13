# Binary NBT formats

## File NBT

`BinaryFormat::File` is the default and represents persisted Java Edition NBT:

```text
Type ID | unsigned 16-bit name length | name bytes | payload
```

Use it for world, player and standalone NBT files. Network NBT before Java 1.20.2 also uses this representation, normally with an empty root name encoded as `00 00`.

## Network NBT

Since Java 1.20.2, protocol 764, Network NBT removes the root compound name and its length entirely:

```text
0A | compound payload
```

Select it explicitly:

```cpp
nbt::ParseOptions options;
options.format = nbt::BinaryFormat::Network;

auto tokens = nbt::tokenize(packet_nbt, options);
auto root = nbt::parse(packet_nbt, tokens, options);
auto encoded = nbt::serialize(root, nbt::BinaryFormat::Network);
```

Network mode requires the root type to be `TAG_Compound`. Nested tags retain their standard names and encoding. This setting does not include packet framing, packet lengths, compression thresholds or protocol fields surrounding the NBT value.

All Java Edition numeric values and lengths are big-endian. `TAG_List` children have no individual type IDs or names because the list header supplies their type.
