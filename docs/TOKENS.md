# Tokenization and source binding

`nbt::tokenize` scans binary NBT without constructing a tree and without copying names or payloads. Token byte offsets are bound to the source span through a `TokenizedDocument` or `TokenizedView`.

## Safe and fast default

```cpp
nbt::TokenizedDocument document = nbt::tokenize(buffer);
nbt::Tag tree = nbt::parse(document);
```

The document retains a non-owning `std::span<const std::byte>` of the source. Parsing the document uses that exact span, making accidental buffer/token mixing impossible without hashing or another full-buffer pass. The caller must keep the source allocation alive and must not mutate or reallocate it while the document is used.

The default `SourceValidation::Identity` compares `data()` and `size()` in `O(1)` when an explicit input is supplied:

```cpp
nbt::Tag tree = nbt::parse(buffer, document);
```

A separate allocation is rejected even if it contains identical bytes. Use document-only parsing when possible.

## Strict content validation

Content validation is opt-in because it reads the complete source during tokenization and again during validation:

```cpp
nbt::ParseOptions options;
options.sourceValidation = nbt::SourceValidation::Content;

auto document = nbt::tokenize(buffer, options);
auto tree = nbt::parse(possibly_copied_buffer, document, options);
```

The 64-bit fingerprint is stored once in the document, never in individual tokens. A byte-for-byte copy is accepted. This is an accidental-integrity guard, not cryptographic authentication.

## No validation

```cpp
nbt::ParseOptions options;
options.sourceValidation = nbt::SourceValidation::None;
auto tree = nbt::parse(externally_verified_buffer, document, options);
```

Use this only when the application enforces source compatibility and lifetime externally.

## Allocation-free token output

```cpp
std::array<nbt::Token, 4096> storage;
nbt::TokenizedView view = nbt::tokenize(buffer, storage);
nbt::Tag tree = nbt::parse(view);
```

This overload performs no heap allocation. It throws `nbt::Error` when the destination is too small. Tokens have 32-bit offsets, occupy at most 24 bytes and support input spans up to 4 GiB.

## Token fields

- `begin`, `end`: half-open byte range `[begin, end)`.
- `parent`: index of the owning tag, or `Token::noParent` for the root.
- `subtreeEnd`: first token index after the tag subtree.
- `count`: list or compound child count.
- `type`: NBT tag type.
- `elementType`: homogeneous list element type.
- `kind`: tag, name or payload.

Source pointer, size, optional fingerprint and binary format are document metadata and do not increase every token's size.
