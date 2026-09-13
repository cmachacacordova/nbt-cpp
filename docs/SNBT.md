# SNBT

Parse and emit stringified NBT with:

```cpp
nbt::Tag root = nbt::parseSnbt(R"({name:"Alex",health:20s,enabled:true})");
std::string compact = nbt::toSnbt(root, false);
std::string pretty = nbt::toSnbt(root, true);
```

Supported values include compounds, homogeneous lists, quoted and unquoted strings, booleans represented as bytes, numeric suffixes, decimal/exponent notation and typed arrays:

```snbt
[B;1b,-2b]
[I;1,-2]
[L;1L,-2L]
```

SNBT parsing builds an owning tree and is separate from binary zero-copy tokenization. `ParseOptions::maxDepth` and `maxElements` apply to SNBT parsing.
