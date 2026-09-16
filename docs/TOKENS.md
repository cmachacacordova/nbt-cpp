# Token indexing

`tokenize(std::istream&)` walks binary NBT and returns structural tokens with stream-relative offsets. Tokens do not own or reference source bytes.

```cpp
std::ifstream input("data.nbt", std::ios::binary);
auto document = nbt::tokenize(input);
```

`TokenizedDocument` owns its token vector and records the binary format. A separate non-owning token view is intentionally not exposed: without source-backed typed views or a genuinely allocation-free stream tokenizer, it would duplicate `std::span<const Token>` without adding useful semantics.

The encoded document size is available directly from its root token:

```cpp
std::size_t bytes = nbt::encodedSize(document);
```

Each token remains at most 24 bytes and records type, kind, byte offsets, parent, subtree boundary, count, and list element type.
