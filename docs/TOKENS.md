# Structural index

The lazy codec builds a private structural index while validating input. Index entries store 32-bit offsets and metadata; they never store pointers into source values.

The index is intentionally not public. Users navigate through `Nbt::TagView`, which contains only a document pointer and node index, or decode an owning `nbt::Tag` tree through `Nbt::materialize()`. This preserves freedom to change the compact index layout without breaking the API.

## Incremental reuse

Nodes are stored in preorder and keep a `Node::END` sentinel in `end`/`payload` while incomplete. When `append` triggers revalidation, the parse replays deterministically over the same byte prefix and `beginNode` reuses existing nodes in preorder via `next_node_`. A node whose `end` is already resolved is skipped entirely: `position_` jumps to `node.end` and `next_node_` jumps to `node.nextSibling`, skipping the whole subtree. Only the truncated frontier is re-examined, so `append` does not re-scan already validated input.
