#pragma once

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "nbt/export.h"

namespace nbt {

using Byte = std::int8_t;
using ByteArray = std::vector<std::int8_t>;
using IntArray = std::vector<std::int32_t>;
using LongArray = std::vector<std::int64_t>;

class Buffer {
public:
  struct Ring {
    std::span<const std::byte> data;
    std::shared_ptr<Ring> next;
    std::shared_ptr<std::vector<std::byte>> owner;
  };

  Buffer() = default;

  explicit Buffer(std::size_t size, std::byte value = std::byte{0}) : size_(size) {
    if (size > 0) {
      auto owner = std::make_shared<std::vector<std::byte>>(size, value);
      auto ring = std::make_shared<Ring>();
      ring->data = std::span<const std::byte>{owner->data(), owner->size()};
      ring->owner = owner;
      head_ = tail_ = ring;
    }
  }

  Buffer(const void *data, std::size_t size) : size_(size) {
    if (size > 0) {
      auto owner = std::make_shared<std::vector<std::byte>>(size);
      std::memcpy(owner->data(), data, size);
      auto ring = std::make_shared<Ring>();
      ring->data = std::span<const std::byte>{owner->data(), owner->size()};
      ring->owner = owner;
      head_ = tail_ = ring;
    }
  }

  Buffer(std::span<const std::byte> view) : size_(view.size()) {
    if (!view.empty()) {
      auto ring = std::make_shared<Ring>();
      ring->data = view;
      head_ = tail_ = ring;
    }
  }

  Buffer(std::vector<std::byte> &&vec) : size_(vec.size()) {
    if (!vec.empty()) {
      auto owner = std::make_shared<std::vector<std::byte>>(std::move(vec));
      auto ring = std::make_shared<Ring>();
      ring->data = std::span<const std::byte>{owner->data(), owner->size()};
      ring->owner = owner;
      head_ = tail_ = ring;
    }
  }

  Buffer(const std::vector<std::byte> &vec) : Buffer(vec.data(), vec.size()) {
  }

  Buffer(const Buffer &other) : size_(other.size_) {
    for (auto *ring = other.head(); ring != nullptr; ring = ring->next.get()) {
      auto newRing = std::make_shared<Ring>();
      if (!ring->data.empty()) {
        auto owner = std::make_shared<std::vector<std::byte>>(ring->data.begin(), ring->data.end());
        newRing->data = std::span<const std::byte>{owner->data(), owner->size()};
        newRing->owner = owner;
      }
      appendRing(newRing);
    }
  }

  Buffer &operator=(const Buffer &other) {
    if (this != &other) {
      Buffer copy(other);
      *this = std::move(copy);
    }
    return *this;
  }

  Buffer(Buffer &&) = default;
  Buffer &operator=(Buffer &&) = default;

  Buffer &append(std::span<const unsigned char> chunk) {
    return append(std::as_bytes(chunk));
  }

  Buffer &append(std::span<const std::byte> chunk) {
    if (chunk.empty()) {
      return *this;
    }
    auto ring = std::make_shared<Ring>();
    ring->data = chunk;
    appendRing(ring);
    size_ += chunk.size();
    cache_.reset();
    return *this;
  }

  Buffer &append(const void *data, std::size_t size) {
    return append(std::span<const std::byte>{static_cast<const std::byte *>(data), size});
  }

  Buffer &append(const std::vector<std::byte> &vec) {
    return append(std::span<const std::byte>{vec});
  }

  Buffer &append(const Buffer &buffer) {
    for (auto *ring = buffer.head(); ring != nullptr; ring = ring->next.get()) {
      auto newRing = std::make_shared<Ring>();
      newRing->data = ring->data;
      newRing->owner = ring->owner;
      appendRing(newRing);
      size_ += ring->data.size();
    }
    cache_.reset();
    return *this;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] bool isContiguous() const noexcept {
    return head_ == nullptr || head_ == tail_;
  }

  [[nodiscard]] const Ring *head() const noexcept {
    return head_.get();
  }

  [[nodiscard]] std::span<const std::byte> contiguous() const {
    if (head_ == nullptr) {
      return {};
    }
    if (isContiguous()) {
      return head_->data;
    }
    if (!cache_) {
      cache_ = std::make_shared<std::vector<std::byte>>();
      cache_->reserve(size_);
      for (auto *ring = head_.get(); ring != nullptr; ring = ring->next.get()) {
        cache_->insert(cache_->end(), ring->data.begin(), ring->data.end());
      }
    }
    return std::span<const std::byte>{cache_->data(), cache_->size()};
  }

  [[nodiscard]] const std::byte *data() const {
    return contiguous().data();
  }

  [[nodiscard]] std::byte *data() {
    if (head_ == nullptr) {
      return nullptr;
    }
    if (head_ == tail_ && head_->owner != nullptr) {
      return head_->owner->data();
    }
    *this = flatten();
    return head_->owner->data();
  }

  [[nodiscard]] std::byte operator[](std::size_t index) const {
    return contiguous()[index];
  }

  [[nodiscard]] std::byte &operator[](std::size_t index) {
    return data()[index];
  }

  [[nodiscard]] std::byte front() const {
    return contiguous()[0];
  }

  [[nodiscard]] const std::byte *begin() const {
    return data();
  }

  [[nodiscard]] const std::byte *end() const {
    return data() + size_;
  }

  [[nodiscard]] Buffer flatten() const {
    if (head_ == nullptr) {
      return {};
    }
    std::vector<std::byte> result;
    result.reserve(size_);
    for (auto *ring = head_.get(); ring != nullptr; ring = ring->next.get()) {
      result.insert(result.end(), ring->data.begin(), ring->data.end());
    }
    return Buffer{std::move(result)};
  }

  bool operator==(const Buffer &other) const {
    if (size_ != other.size_) {
      return false;
    }
    const auto left = contiguous();
    const auto right = other.contiguous();
    return std::memcmp(left.data(), right.data(), left.size()) == 0;
  }

  bool operator!=(const Buffer &other) const {
    return !(*this == other);
  }

private:
  void appendRing(const std::shared_ptr<Ring> &ring) {
    if (head_ == nullptr) {
      head_ = tail_ = ring;
    } else {
      tail_->next = ring;
      tail_ = ring;
    }
    ring->next = nullptr;
  }

  std::shared_ptr<Ring> head_;
  std::shared_ptr<Ring> tail_;
  std::size_t size_{};
  mutable std::shared_ptr<std::vector<std::byte>> cache_;
};

enum class Type : std::uint8_t { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6, ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12 };

enum class Compression : std::uint8_t { None, Gzip, Zlib, Auto };
enum class BinaryFormat : std::uint8_t { File, Network };
enum class SourceValidation : std::uint8_t { Identity, Content, None };
enum class TokenKind : std::uint8_t { Tag, Name, Payload };

class NBT_CPP_API Error : public std::runtime_error {
public:
  Error(std::string message, std::size_t offset);
  [[nodiscard]] std::size_t offset() const noexcept;

private:
  std::size_t offset_;
};

struct Tag;

struct List {
  std::vector<Tag> values;
};

struct Compound {
  std::vector<Tag> values;
};

using Value = std::variant<std::monostate, Byte, std::int16_t, std::int32_t, std::int64_t, float, double, ByteArray, std::string, List, Compound, IntArray, LongArray>;

struct NBT_CPP_API Tag {
  Type type{Type::End};
  std::string name;
  Value value;
  Type elementType{Type::End};

  Tag() = default;
  Tag(Type type, std::string name, Value value, Type elementType = Type::End);

  static Tag byte(std::string name, Byte value);
  static Tag shortTag(std::string name, std::int16_t value);
  static Tag intTag(std::string name, std::int32_t value);
  static Tag longTag(std::string name, std::int64_t value);
  static Tag floatTag(std::string name, float value);
  static Tag doubleTag(std::string name, double value);
  static Tag byteArray(std::string name, ByteArray value);
  static Tag string(std::string name, std::string value);
  static Tag list(std::string name, Type elementType, std::vector<Tag> value = {});
  static Tag compound(std::string name, std::vector<Tag> value = {});
  static Tag intArray(std::string name, IntArray value);
  static Tag longArray(std::string name, LongArray value);

  template <class T>
  [[nodiscard]] T &as() {
    return std::get<T>(value);
  }

  template <class T>
  [[nodiscard]] const T &as() const {
    return std::get<T>(value);
  }
};

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
      return std::bit_cast<double>(detail::readUnsigned(source_, entry.begin, 8));
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

[[nodiscard]] NBT_CPP_API TokenizedDocument tokenize(std::span<const std::byte> input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API TokenizedView tokenize(std::span<const std::byte> input, std::span<Token> output, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(std::span<const std::byte> input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const Buffer &input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(std::span<const std::byte> input, const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(std::span<const std::byte> input, const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const Buffer &input, const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const Buffer &input, const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Buffer serialize(const Tag &root, BinaryFormat format = BinaryFormat::File);

[[nodiscard]] NBT_CPP_API Buffer compress(std::span<const std::byte> input, Compression compression);
[[nodiscard]] NBT_CPP_API Buffer compress(const Buffer &input, Compression compression);
[[nodiscard]] NBT_CPP_API Buffer decompress(std::span<const std::byte> input, Compression compression = Compression::Auto);
[[nodiscard]] NBT_CPP_API Buffer decompress(const Buffer &input, Compression compression = Compression::Auto);
[[nodiscard]] NBT_CPP_API Tag load(const std::filesystem::path &path, Compression compression = Compression::Auto, const ParseOptions &options = {});
NBT_CPP_API void save(const std::filesystem::path &path, const Tag &root, Compression compression = Compression::None);

[[nodiscard]] NBT_CPP_API Tag clone(const Tag &tag);
using Visitor = std::function<bool(Tag &)>;
using ConstVisitor = std::function<bool(const Tag &)>;
using Predicate = std::function<bool(const Tag &)>;
NBT_CPP_API bool map(Tag &root, const Visitor &visitor);
NBT_CPP_API bool map(const Tag &root, const ConstVisitor &visitor);
[[nodiscard]] NBT_CPP_API std::optional<Tag> filter(const Tag &root, const Predicate &predicate);
NBT_CPP_API void filterInPlace(Tag &root, const Predicate &predicate);
[[nodiscard]] NBT_CPP_API Tag *find(Tag &root, const Predicate &predicate);
[[nodiscard]] NBT_CPP_API const Tag *find(const Tag &root, const Predicate &predicate);
[[nodiscard]] NBT_CPP_API Tag *findByName(Tag &root, std::string_view name);
[[nodiscard]] NBT_CPP_API const Tag *findByName(const Tag &root, std::string_view name);
[[nodiscard]] NBT_CPP_API Tag *findByPath(Tag &root, std::string_view path);
[[nodiscard]] NBT_CPP_API const Tag *findByPath(const Tag &root, std::string_view path);
[[nodiscard]] NBT_CPP_API Tag *at(Tag &tag, std::size_t index);
[[nodiscard]] NBT_CPP_API const Tag *at(const Tag &tag, std::size_t index);
[[nodiscard]] NBT_CPP_API std::size_t size(const Tag &root);
[[nodiscard]] NBT_CPP_API bool equivalent(const Tag &lhs, const Tag &rhs, double eps = 1e-6);
[[nodiscard]] NBT_CPP_API Tag parseSnbt(std::string_view input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API std::string toSnbt(const Tag &root, bool pretty = true);
[[nodiscard]] NBT_CPP_API std::string_view typeName(Type type) noexcept;

class NBT_CPP_API Builder {
public:
  explicit Builder(std::string rootName = {});
  Builder &add(Tag tag);
  Builder &beginCompound(std::string name);
  Builder &beginList(std::string name, Type elementType);
  Builder &end();
  [[nodiscard]] Tag build() const;

private:
  Tag root_;
  std::vector<Tag *> stack_;
};

} // namespace nbt
