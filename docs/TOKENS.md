# Structural index

The lazy codec builds a private structural index while validating input. Index entries store 32-bit offsets and metadata; they never store pointers into source values.

The index is intentionally not public. Users navigate through `Nbt::View`, which contains only a document pointer and node index. This preserves freedom to change the compact index layout without breaking the API.
