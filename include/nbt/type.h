#pragma once

#include <cstdint>

namespace nbt {

/** Binary identifiers defined by the Java Edition NBT specification. */
enum class Type : std::uint8_t { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6, ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12 };

/** Compression applied by the filesystem convenience functions. */
enum class Compression : std::uint8_t {
  None, ///< Read or write plain NBT bytes.
  Gzip, ///< Read or write a gzip stream.
  Zlib, ///< Read or write a zlib stream.
  Auto  ///< Detect plain, gzip, or zlib input; invalid for writing.
};

/** Root encoding used for binary NBT. */
enum class BinaryFormat : std::uint8_t {
  File,   ///< Named-root Java Edition file encoding.
  Network ///< Unnamed compound-root encoding used by modern Java protocol packets.
};

/** Role of a token in a tokenized binary document. */
enum class TokenKind : std::uint8_t { Tag, Name, Payload };

} // namespace nbt
