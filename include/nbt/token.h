#pragma once

#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "nbt/error.h"
#include "nbt/export.h"
#include "nbt/tag.h"
#include "nbt/type.h"

namespace nbt {

struct Token {
  static constexpr std::uint32_t noParent = UINT32_MAX;

  std::uint32_t begin{};
  std::uint32_t end{};
  std::uint32_t parent{noParent};
  std::uint32_t subtreeEnd{};
  std::uint32_t count{};
  Type type{Type::End};
  Type elementType{Type::End};
  TokenKind kind{};

  bool operator==(const Token &) const = default;
};

static_assert(sizeof(Token) <= 24);

struct ParseOptions {
  std::size_t maxDepth{512};
  std::size_t maxElements{16 * 1024 * 1024};
  bool requireCompleteInput{true};
  BinaryFormat format{BinaryFormat::File};
  SourceValidation sourceValidation{SourceValidation::Identity};
};

template <Type T>
class TagView;

struct TokenizedDocument {
  std::span<const std::byte> source;
  std::vector<Token> tokens;
  std::uint64_t fingerprint{};
  bool hasFingerprint{};
  BinaryFormat format{BinaryFormat::File};

  template <Type T>
  [[nodiscard]] TagView<T> get(std::string_view name) const;
  template <Type T>
  [[nodiscard]] std::optional<TagView<T>> find(std::string_view name) const;
  template <Type T>
  [[nodiscard]] TagView<T> getPath(std::string_view path) const;
};

struct TokenizedView {
  std::span<const std::byte> source;
  std::span<const Token> tokens;
  std::uint64_t fingerprint{};
  bool hasFingerprint{};
  BinaryFormat format{BinaryFormat::File};

  template <Type T>
  [[nodiscard]] TagView<T> get(std::string_view name) const;
  template <Type T>
  [[nodiscard]] std::optional<TagView<T>> find(std::string_view name) const;
  template <Type T>
  [[nodiscard]] TagView<T> getPath(std::string_view path) const;
};

namespace detail {
[[nodiscard]] NBT_CPP_API std::optional<std::uint32_t> findChild(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view expected) noexcept;
[[nodiscard]] NBT_CPP_API std::optional<std::uint32_t> findPath(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view path) noexcept;
[[nodiscard]] NBT_CPP_API std::optional<std::uint32_t> listItem(std::span<const Token> tokens, std::uint32_t parent, std::size_t index) noexcept;
[[nodiscard]] NBT_CPP_API const Token &payload(std::span<const Token> tokens, std::uint32_t tag);
[[nodiscard]] NBT_CPP_API std::string_view name(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t tag) noexcept;
[[nodiscard]] NBT_CPP_API std::uint64_t readUnsigned(std::span<const std::byte> source, std::uint32_t begin, std::size_t size);
} // namespace detail

class NBT_CPP_API IntArrayView {
public:
  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] std::int32_t operator[](std::size_t index) const;

private:
  template <Type>
  friend class TagView;

  IntArrayView(std::span<const std::byte> source, std::uint32_t begin, std::size_t size) : source_(source), begin_(begin), size_(size) {
  }

  std::span<const std::byte> source_;
  std::uint32_t begin_{};
  std::size_t size_{};
};

class NBT_CPP_API LongArrayView {
public:
  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] std::int64_t operator[](std::size_t index) const;

private:
  template <Type>
  friend class TagView;

  LongArrayView(std::span<const std::byte> source, std::uint32_t begin, std::size_t size) : source_(source), begin_(begin), size_(size) {
  }

  std::span<const std::byte> source_;
  std::uint32_t begin_{};
  std::size_t size_{};
};

template <Type T>
class TagView {
public:
  [[nodiscard]] static constexpr Type type() noexcept {
    return T;
  }

  [[nodiscard]] std::string_view name() const noexcept {
    return detail::name(source_, tokens_, index_);
  }

  [[nodiscard]] auto value() const {
    const auto &entry = detail::payload(tokens_, index_);
    if constexpr (T == Type::Byte) {
      return static_cast<Byte>(detail::readUnsigned(source_, entry.begin, 1));
    } else if constexpr (T == Type::Short) {
      return static_cast<std::int16_t>(detail::readUnsigned(source_, entry.begin, 2));
    } else if constexpr (T == Type::Int) {
      return static_cast<std::int32_t>(detail::readUnsigned(source_, entry.begin, 4));
    } else if constexpr (T == Type::Long) {
      return static_cast<std::int64_t>(detail::readUnsigned(source_, entry.begin, 8));
    } else if constexpr (T == Type::Float) {
      return std::bit_cast<float>(static_cast<std::uint32_t>(detail::readUnsigned(source_, entry.begin, 4)));
    } else if constexpr (T == Type::Double) {
      return std::bit_cast<double>(static_cast<std::uint64_t>(detail::readUnsigned(source_, entry.begin, 8)));
    } else if constexpr (T == Type::String) {
      const auto length = detail::readUnsigned(source_, entry.begin, 2);
      return std::string_view(reinterpret_cast<const char *>(source_.data() + entry.begin + 2), length);
    } else if constexpr (T == Type::ByteArray) {
      const auto length = detail::readUnsigned(source_, entry.begin, 4);
      return std::span<const Byte>(reinterpret_cast<const Byte *>(source_.data() + entry.begin + 4), length);
    } else if constexpr (T == Type::IntArray) {
      const auto length = detail::readUnsigned(source_, entry.begin, 4);
      return IntArrayView{source_, entry.begin + 4, static_cast<std::size_t>(length)};
    } else if constexpr (T == Type::LongArray) {
      const auto length = detail::readUnsigned(source_, entry.begin, 4);
      return LongArrayView{source_, entry.begin + 4, static_cast<std::size_t>(length)};
    } else {
      static_assert(T != T, "container tags do not have scalar values");
    }
  }

  template <Type U>
  [[nodiscard]] TagView<U> get(std::string_view name) const {
    const auto found = detail::findChild(source_, tokens_, index_, name);
    if (!found || tokens_[*found].type != U) {
      throw Error("NBT child not found or has a different type", tokens_[index_].begin);
    }
    return {source_, tokens_, *found};
  }

  template <Type U>
  [[nodiscard]] TagView<U> at(std::size_t position) const {
    const auto found = detail::listItem(tokens_, index_, position);
    if (!found || tokens_[*found].type != U) {
      throw Error("NBT list item not found or has a different type", tokens_[index_].begin);
    }
    return {source_, tokens_, *found};
  }

private:
  friend struct TokenizedDocument;
  friend struct TokenizedView;
  template <Type>
  friend class TagView;

  TagView(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t index) : source_(source), tokens_(tokens), index_(index) {
  }

  std::span<const std::byte> source_;
  std::span<const Token> tokens_;
  std::uint32_t index_{};
};

template <Type T>
TagView<T> TokenizedDocument::get(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T) {
    throw Error("NBT tag not found or has a different type", 0);
  }
  return {source, tokens, *found};
}

template <Type T>
std::optional<TagView<T>> TokenizedDocument::find(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T) {
    return std::nullopt;
  }
  return TagView<T>{source, tokens, *found};
}

template <Type T>
TagView<T> TokenizedDocument::getPath(std::string_view path) const {
  const auto found = detail::findPath(source, tokens, 0, path);
  if (!found || tokens[*found].type != T) {
    throw Error("NBT path not found or has a different type", 0);
  }
  return {source, tokens, *found};
}

template <Type T>
TagView<T> TokenizedView::get(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T) {
    throw Error("NBT tag not found or has a different type", 0);
  }
  return {source, tokens, *found};
}

template <Type T>
std::optional<TagView<T>> TokenizedView::find(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T) {
    return std::nullopt;
  }
  return TagView<T>{source, tokens, *found};
}

template <Type T>
TagView<T> TokenizedView::getPath(std::string_view path) const {
  const auto found = detail::findPath(source, tokens, 0, path);
  if (!found || tokens[*found].type != T) {
    throw Error("NBT path not found or has a different type", 0);
  }
  return {source, tokens, *found};
}

} // namespace nbt
