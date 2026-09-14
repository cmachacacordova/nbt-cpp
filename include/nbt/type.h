#pragma once

#include <cstdint>

namespace nbt {

enum class Type : std::uint8_t { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6, ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12 };

enum class Compression : std::uint8_t { None, Gzip, Zlib, Auto };

enum class BinaryFormat : std::uint8_t { File, Network };

enum class SourceValidation : std::uint8_t { Identity, Content, None };

enum class TokenKind : std::uint8_t { Tag, Name, Payload };

} // namespace nbt
