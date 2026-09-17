# SNBT utilities

SNBT is separated from the binary codec:

```cpp
#include <nbt/utilities.h>

auto value = nbt::NbtUtilities::parseSnbt("{name:\"Alex\",health:20s}");
auto compact = nbt::NbtUtilities::toSnbt(value);
auto pretty = nbt::NbtUtilities::toSnbt(value, true);
```

The parser supports compounds, homogeneous lists, quoted and unquoted strings, booleans, numeric suffixes and typed byte/int/long arrays. It applies depth and per-container limits from `Nbt::Options`.
