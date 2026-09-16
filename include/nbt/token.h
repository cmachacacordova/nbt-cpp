#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nbt/type.h"

namespace nbt {

/** Structural index entry whose offsets are relative to the tokenized stream position. */
struct Token {
  /** Sentinel used when a token has no parent. */
  static constexpr std::uint32_t noParent = UINT32_MAX;

  std::uint32_t begin{};          ///< First byte belonging to this entry.
  std::uint32_t end{};            ///< One-past-last byte belonging to this entry.
  std::uint32_t parent{noParent}; ///< Parent token index, or noParent.
  std::uint32_t subtreeEnd{};     ///< One-past-last token in this tag's subtree.
  std::uint32_t count{};          ///< Element count for list and compound containers.
  Type type{Type::End};           ///< NBT type represented by this entry.
  Type elementType{Type::End};    ///< Element type when this entry represents a list.
  TokenKind kind{};               ///< Structural role of this entry.

  /** Compares all token metadata fields. */
  bool operator==(const Token &) const = default;
};

static_assert(sizeof(Token) <= 24);

/** Resource limits and binary-format selection for parsing and tokenization. */
struct ParseOptions {
  std::size_t maxDepth{512};                              ///< Maximum nested list/compound depth.
  std::size_t maxElements{std::size_t{16} * 1024 * 1024}; ///< Maximum elements in one container.
  bool requireCompleteInput{true};                        ///< Reject bytes following the root document.
  BinaryFormat format{BinaryFormat::File};                ///< Expected binary root encoding.
};

/** Owning token index for one binary NBT document; source bytes are not retained. */
struct TokenizedDocument {
  std::vector<Token> tokens;               ///< Tokens in depth-first document order.
  BinaryFormat format{BinaryFormat::File}; ///< Format used while indexing.
};

} // namespace nbt
