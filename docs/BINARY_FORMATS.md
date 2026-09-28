# NBT binary structure

NBT stores multi-byte numbers in **big-endian** order: the most significant byte comes first.

Strings use **UTF-8**. Both the tag name length and the string payload length count **encoded bytes**, not characters.

## Type identifiers

| ID | Hex | Type |
|---:|---:|---|
| 0 | `00` | `TAG_End` |
| 1 | `01` | `TAG_Byte` |
| 2 | `02` | `TAG_Short` |
| 3 | `03` | `TAG_Int` |
| 4 | `04` | `TAG_Long` |
| 5 | `05` | `TAG_Float` |
| 6 | `06` | `TAG_Double` |
| 7 | `07` | `TAG_Byte_Array` |
| 8 | `08` | `TAG_String` |
| 9 | `09` | `TAG_List` |
| 10 | `0A` | `TAG_Compound` |
| 11 | `0B` | `TAG_Int_Array` |
| 12 | `0C` | `TAG_Long_Array` |

## Named tag format

Outside a list, every tag is encoded as:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | type `u8` | name length `u16` | UTF-8 text | depends on type |
| **Data** | `TT` | `LL LL` | UTF-8 bytes of the name | payload bytes |

`TAG_End` is not a named tag; it only terminates a `TAG_Compound`.

---

## TAG_End (`0x00`)

| | Type ID |
|---|---|
| **Decoded** | 0 |
| **Data** | `00` |

It has no name and no payload. It marks the end of a compound.

---

## TAG_Byte (`0x01`)

Example: `TAG_Byte("byteTest", -2)`

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 1 | 8 | `byteTest` | -2 |
| **Data** | `01` | `00 08` | `62 79 74 65 54 65 73 74` | `FE` |

The payload is a signed 8-bit integer.

---

## TAG_Short (`0x02`)

Example: `TAG_Short("shortTest", 32767)`

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 2 | 9 | `shortTest` | 32767 |
| **Data** | `02` | `00 09` | `73 68 6F 72 74 54 65 73 74` | `7F FF` |

The payload is a signed 16-bit big-endian integer.

---

## TAG_Int (`0x03`)

Example: `TAG_Int("intTest", 2147483647)`

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 3 | 7 | `intTest` | 2147483647 |
| **Data** | `03` | `00 07` | `69 6E 74 54 65 73 74` | `7F FF FF FF` |

The payload is a signed 32-bit big-endian integer.

---

## TAG_Long (`0x04`)

Example: `TAG_Long("longTest", 9223372036854775807)`

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 4 | 8 | `longTest` | 9223372036854775807 |
| **Data** | `04` | `00 08` | `6C 6F 6E 67 54 65 73 74` | `7F FF FF FF FF FF FF FF` |

The payload is a signed 64-bit big-endian integer.

---

## TAG_Float (`0x05`)

Example: `TAG_Float("floatTest", 1.0)`

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 5 | 9 | `floatTest` | 1.0 |
| **Data** | `05` | `00 09` | `66 6C 6F 61 74 54 65 73 74` | `3F 80 00 00` |

The payload is a 32-bit IEEE 754 floating-point value in big-endian byte order.

---

## TAG_Double (`0x06`)

Example: `TAG_Double("doubleTest", 1.0)`

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 6 | 10 | `doubleTest` | 1.0 |
| **Data** | `06` | `00 0A` | `64 6F 75 62 6C 65 54 65 73 74` | `3F F0 00 00 00 00 00 00` |

The payload is a 64-bit IEEE 754 floating-point value in big-endian byte order.

---

## TAG_Byte_Array (`0x07`)

Example: `TAG_Byte_Array("byteArrTest", [1, -2])`

Full tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 7 | 11 | `byteArrTest` | `[1, -2]` |
| **Data** | `07` | `00 0B` | `62 79 74 65 41 72 72 54 65 73 74` | `00 00 00 02 01 FE` |

Payload breakdown:

| | Length | Element 0 | Element 1 |
|---|---|---|---|
| **Decoded** | 2 | 1 | -2 |
| **Data** | `00 00 00 02` | `01` | `FE` |

The payload starts with a signed 32-bit length, followed by that many signed 8-bit integers.

---

## TAG_String (`0x08`)

Example: `TAG_String("stringTest", "hi")`

Full tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 8 | 10 | `stringTest` | `"hi"` |
| **Data** | `08` | `00 0A` | `73 74 72 69 6E 67 54 65 73 74` | `00 02 68 69` |

Payload breakdown:

| | String Length | String Bytes |
|---|---|---|
| **Decoded** | 2 | `hi` |
| **Data** | `00 02` | `68 69` |

The payload is an unsigned 16-bit length followed by that many UTF-8 bytes.

---

## TAG_List (`0x09`)

Example: `TAG_List("listTest", TAG_Short, [1, 2, 3])`

Full tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 9 | 8 | `listTest` | `List<Short> [1, 2, 3]` |
| **Data** | `09` | `00 08` | `6C 69 73 74 54 65 73 74` | `02 00 00 00 03 00 01 00 02 00 03` |

Payload breakdown:

| | Element Type | Length | Element 0 | Element 1 | Element 2 |
|---|---|---|---|---|---|
| **Decoded** | `TAG_Short` | 3 | 1 | 2 | 3 |
| **Data** | `02` | `00 00 00 03` | `00 01` | `00 02` | `00 03` |

The payload contains: element type `u8`, length `i32`, and then each element's payload with no type or name.

The element type byte is always present, even when the list contains no elements. Reading it is the only way to know what type the list is intended to hold.

An empty list conventionally uses `TAG_End` as its element type:

| | Element Type | Length |
|---|---|---|
| **Decoded** | `TAG_End` | 0 |
| **Data** | `00` | `00 00 00 00` |

The original Notchian implementation writes `TAG_End` for empty lists. Some later Mojang implementations write `TAG_Byte` (or the type the list would hold if it had elements) instead. Because a length of `0` means no element payloads follow, a parser should accept any element type when the list length is `0` or negative.

---

## TAG_Compound (`0x0A`)

Example: `TAG_Compound("compoundTest", { TAG_Byte("a", 1) })`

Full tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 10 | 12 | `compoundTest` | `{ "a": 1 }` |
| **Data** | `0A` | `00 0C` | `63 6F 6D 70 6F 75 6E 64 54 65 73 74` | `01 00 01 61 01 00` |

Payload breakdown:

| | Child Type | Child Name | Child Payload | Terminator |
|---|---|---|---|---|
| **Decoded** | `TAG_Byte` | `a` | 1 | `TAG_End` |
| **Data** | `01` | `00 01 61` | `01` | `00` |

A compound does not store its child count. It always ends with `00`.

Empty compound:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 10 | 1 | `c` | `{}` |
| **Data** | `0A` | `00 01` | `63` | `00` |

---

## TAG_Int_Array (`0x0B`)

Example: `TAG_Int_Array("intArrTest", [1, -2])`

Full tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 11 | 10 | `intArrTest` | `[1, -2]` |
| **Data** | `0B` | `00 0A` | `69 6E 74 41 72 72 54 65 73 74` | `00 00 00 02 00 00 00 01 FF FF FF FE` |

Payload breakdown:

| | Length | Element 0 | Element 1 |
|---|---|---|---|
| **Decoded** | 2 | 1 | -2 |
| **Data** | `00 00 00 02` | `00 00 00 01` | `FF FF FF FE` |

The payload starts with a signed 32-bit length, followed by that many signed 32-bit integers.

---

## TAG_Long_Array (`0x0C`)

Example: `TAG_Long_Array("longArrTest", [1, -2])`

Full tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | 12 | 11 | `longArrTest` | `[1, -2]` |
| **Data** | `0C` | `00 0B` | `6C 6F 6E 67 41 72 72 54 65 73 74` | `00 00 00 02 00 00 00 00 00 00 00 01 FF FF FF FF FF FF FF FE` |

Payload breakdown:

| | Length | Element 0 | Element 1 |
|---|---|---|---|
| **Decoded** | 2 | 1 | -2 |
| **Data** | `00 00 00 02` | `00 00 00 00 00 00 00 01` | `FF FF FF FF FF FF FF FE` |

The payload starts with a signed 32-bit length, followed by that many signed 64-bit integers.

---

## Named-root NBT (file format)

The whole document is a single named tag. Example: `TAG_Compound("Level", {})`.

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | `TAG_Compound` | 5 | `Level` | empty compound |
| **Data** | `0A` | `00 05` | `4C 65 76 65 6C` | `00` |

Contiguous bytes:

```text
0A 00 05 4C 65 76 65 6C 00
```

---

## Unnamed compound-root NBT (network format)

Example: empty unnamed compound root.

| | Type ID | Payload |
|---|---|---|
| **Decoded** | `TAG_Compound` | empty compound |
| **Data** | `0A` | `00` |

Contiguous bytes:

```text
0A 00
```

There is no root name length and no root name.

---

## Elements inside a list

Inside a list, elements have no name and no individual type; only their payload is written. The list header already declared the common type.

### TAG_Byte elements

List `[5, -1]`:

| | Element Type | Length | Element 0 | Element 1 |
|---|---|---|---|---|
| **Decoded** | `TAG_Byte` | 2 | 5 | -1 |
| **Data** | `01` | `00 00 00 02` | `05` | `FF` |

### TAG_String elements

List `["hi", "by"]`:

| | Element Type | Length | Element 0 | Element 1 |
|---|---|---|---|---|
| **Decoded** | `TAG_String` | 2 | `hi` | `by` |
| **Data** | `08` | `00 00 00 02` | `00 02 68 69` | `00 02 62 79` |

### TAG_Compound elements

List `[{ "a": 1 }, { "b": 2 }]`:

| | Element Type | Length | Payload 0 | Payload 1 |
|---|---|---|---|---|
| **Decoded** | `TAG_Compound` | 2 | `TAG_Byte("a",1)` + `TAG_End` | `TAG_Byte("b",2)` + `TAG_End` |
| **Data** | `0A` | `00 00 00 02` | `01 00 01 61 01 00` | `01 00 01 62 02 00` |

### Nested TAG_List elements

List of two float lists, each with one element:

| | Element Type | Length | Payload 0 | Payload 1 |
|---|---|---|---|---|
| **Decoded** | `TAG_List` | 2 | `List<Float>[1.0]` | `List<Float>[2.0]` |
| **Data** | `09` | `00 00 00 02` | `05 00 00 00 01 3F 80 00 00` | `05 00 00 00 01 40 00 00 00` |

---

## Valid combinations

```text
Document
|
+-- root: one named tag (File) or one unnamed compound (Network)

TAG_Compound
|
+-- children: NamedTag(any non-End type) ...
+-- TAG_End (1 byte)

TAG_List<T>
|
+-- elements: payload(T) with no name ...

primitive, String, or Array tag
|
+-- payload only, no children
```

### Complex tree example

```text
TAG_Compound "root"
|
+-- TAG_String "name" -> "Alex"
+-- TAG_List<TAG_Compound> "players"
|   |
|   +-- payload(Compound)
|   |   +-- TAG_Int "score" -> 100
|   |   +-- TAG_End
|   |
|   +-- payload(Compound)
|       +-- TAG_Int "score" -> 200
|       +-- TAG_End
|
+-- TAG_Byte_Array "data" -> [10, 20]
+-- TAG_End
```

---

## Complete named-root document example

Named compound root `"Root"` containing:

- `TAG_Byte("ok", 1)`
- `TAG_String("msg", "hi")`
- `TAG_List("nums", TAG_Short, [10, 20])`
- `TAG_Compound("sub", { TAG_Int("id", 7) })`

Root tag:

| | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| **Decoded** | `TAG_Compound` | 4 | `Root` | 4 children + `TAG_End` |
| **Data** | `0A` | `00 04` | `52 6F 6F 74` | see breakdown below |

Root payload breakdown:

| Child | Type ID | Length of Name | Name | Payload |
|---|---|---|---|---|
| 1 | `01` | `00 02` | `6F 6B` | `01` |
| 2 | `08` | `00 03` | `6D 73 67` | `00 02 68 69` |
| 3 | `09` | `00 04` | `6E 75 6D 73` | `02 00 00 00 02 00 0A 00 14` |
| 4 | `0A` | `00 03` | `73 75 62` | `03 00 02 69 64 00 00 00 07 00` |
| End | `00` | — | — | — |

Contiguous bytes of the whole document:

```text
0A 00 04 52 6F 6F 74 01 00 02 6F 6B 01 08 00 03
6D 73 67 00 02 68 69 09 00 04 6E 75 6D 73 02 00
00 00 02 00 0A 00 14 0A 00 03 73 75 62 03 00 02
69 64 00 00 00 07 00 00
```

---

## Invalid combinations

| Situation | Reason |
|---|---|
| Mixing types inside one list | a list is homogeneous |
| Giving names to list elements | elements only carry payloads |
| Encoding `TAG_End` as a named tag | `TAG_End` is only a compound terminator |
| Omitting the final `00` of a compound | the parser would not know where it ends |
| Negative list or array lengths | the length field is signed but negative values are invalid |
| Unnamed root with a type other than `0A` | network format requires a compound root |
| String longer than 65535 encoded bytes | the length field is `u16` |

---

## Format summary

```text
NBT Document
|
+-- [File]    type:u8 + name_len:u16 + name + payload
|     |
|     +-- primitive: fixed-size value
|     +-- String/Array: len + data
|     +-- List: elem_type + len + payload×N
|     +-- Compound: NamedTag* + 0x00
|
+-- [Network] 0x0A + payload(Compound)
```

Every recursive branch ends in: a primitive value, a string, an array, a list of payloads, or a compound closed by `00`.
