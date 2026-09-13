#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nbt {

using Byte = std::int8_t;
using ByteArray = std::vector<std::int8_t>;
using IntArray = std::vector<std::int32_t>;
using LongArray = std::vector<std::int64_t>;
using Buffer = std::vector<std::byte>;

enum class Type : std::uint8_t { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6, ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12 };

enum class Compression { None, Gzip, Zlib, Auto };
enum class BinaryFormat { File, Network };
enum class SourceValidation { Identity, Content, None };
enum class TokenKind : std::uint8_t { Tag, Name, Payload };

class Error : public std::runtime_error {
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

struct Tag {
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

  template <class T> T &as() { return std::get<T>(value); }
  template <class T> const T &as() const { return std::get<T>(value); }
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

template <Type T> class TagView;

struct TokenizedDocument {
  std::span<const std::byte> source;
  std::vector<Token> tokens;
  std::uint64_t fingerprint{};
  bool hasFingerprint{};
  BinaryFormat format{BinaryFormat::File};

  template <Type T> [[nodiscard]] TagView<T> get(std::string_view name) const;
  template <Type T> [[nodiscard]] std::optional<TagView<T>> find(std::string_view name) const;
  template <Type T> [[nodiscard]] TagView<T> getPath(std::string_view path) const;
};

struct TokenizedView {
  std::span<const std::byte> source;
  std::span<const Token> tokens;
  std::uint64_t fingerprint{};
  bool hasFingerprint{};
  BinaryFormat format{BinaryFormat::File};

  template <Type T> [[nodiscard]] TagView<T> get(std::string_view name) const;
  template <Type T> [[nodiscard]] std::optional<TagView<T>> find(std::string_view name) const;
  template <Type T> [[nodiscard]] TagView<T> getPath(std::string_view path) const;
};

namespace detail {
[[nodiscard]] std::optional<std::uint32_t> findChild(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view name) noexcept;
[[nodiscard]] std::optional<std::uint32_t> findPath(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view path) noexcept;
[[nodiscard]] std::optional<std::uint32_t> listItem(std::span<const Token> tokens, std::uint32_t parent, std::size_t index) noexcept;
[[nodiscard]] const Token &payload(std::span<const Token> tokens, std::uint32_t tag);
[[nodiscard]] std::string_view name(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t tag) noexcept;
[[nodiscard]] std::uint64_t readUnsigned(std::span<const std::byte> source, std::uint32_t begin, std::size_t size);
} // namespace detail

class IntArrayView {
public:
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::int32_t operator[](std::size_t index) const;

private:
  template <Type> friend class TagView;
  IntArrayView(std::span<const std::byte> source, std::uint32_t begin, std::size_t size) : source_(source), begin_(begin), size_(size) {}
  std::span<const std::byte> source_;
  std::uint32_t begin_{};
  std::size_t size_{};
};

class LongArrayView {
public:
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::int64_t operator[](std::size_t index) const;

private:
  template <Type> friend class TagView;
  LongArrayView(std::span<const std::byte> source, std::uint32_t begin, std::size_t size) : source_(source), begin_(begin), size_(size) {}
  std::span<const std::byte> source_;
  std::uint32_t begin_{};
  std::size_t size_{};
};

template <Type T> class TagView {
public:
  [[nodiscard]] static constexpr Type type() noexcept { return T; }
  [[nodiscard]] std::string_view name() const noexcept { return detail::name(source_, tokens_, index_); }

  [[nodiscard]] auto value() const {
    const auto &entry = detail::payload(tokens_, index_);
    if constexpr (T == Type::Byte)
      return static_cast<Byte>(detail::readUnsigned(source_, entry.begin, 1));
    else if constexpr (T == Type::Short)
      return static_cast<std::int16_t>(detail::readUnsigned(source_, entry.begin, 2));
    else if constexpr (T == Type::Int)
      return static_cast<std::int32_t>(detail::readUnsigned(source_, entry.begin, 4));
    else if constexpr (T == Type::Long)
      return static_cast<std::int64_t>(detail::readUnsigned(source_, entry.begin, 8));
    else if constexpr (T == Type::Float)
      return std::bit_cast<float>(static_cast<std::uint32_t>(detail::readUnsigned(source_, entry.begin, 4)));
    else if constexpr (T == Type::Double)
      return std::bit_cast<double>(detail::readUnsigned(source_, entry.begin, 8));
    else if constexpr (T == Type::String) {
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

  template <Type U> [[nodiscard]] TagView<U> get(std::string_view name) const {
    const auto found = detail::findChild(source_, tokens_, index_, name);
    if (!found || tokens_[*found].type != U)
      throw Error("NBT child not found or has a different type", tokens_[index_].begin);
    return {source_, tokens_, *found};
  }

  template <Type U> [[nodiscard]] TagView<U> at(std::size_t position) const {
    const auto found = detail::listItem(tokens_, index_, position);
    if (!found || tokens_[*found].type != U)
      throw Error("NBT list item not found or has a different type", tokens_[index_].begin);
    return {source_, tokens_, *found};
  }

private:
  friend struct TokenizedDocument;
  friend struct TokenizedView;
  template <Type> friend class TagView;
  TagView(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t index) : source_(source), tokens_(tokens), index_(index) {}
  std::span<const std::byte> source_;
  std::span<const Token> tokens_;
  std::uint32_t index_{};
};

template <Type T> TagView<T> TokenizedDocument::get(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T)
    throw Error("NBT tag not found or has a different type", 0);
  return {source, tokens, *found};
}
template <Type T> std::optional<TagView<T>> TokenizedDocument::find(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T)
    return std::nullopt;
  return TagView<T>{source, tokens, *found};
}
template <Type T> TagView<T> TokenizedDocument::getPath(std::string_view path) const {
  const auto found = detail::findPath(source, tokens, 0, path);
  if (!found || tokens[*found].type != T)
    throw Error("NBT path not found or has a different type", 0);
  return {source, tokens, *found};
}
template <Type T> TagView<T> TokenizedView::get(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T)
    throw Error("NBT tag not found or has a different type", 0);
  return {source, tokens, *found};
}
template <Type T> std::optional<TagView<T>> TokenizedView::find(std::string_view name) const {
  const auto found = detail::findChild(source, tokens, 0, name);
  if (!found || tokens[*found].type != T)
    return std::nullopt;
  return TagView<T>{source, tokens, *found};
}
template <Type T> TagView<T> TokenizedView::getPath(std::string_view path) const {
  const auto found = detail::findPath(source, tokens, 0, path);
  if (!found || tokens[*found].type != T)
    throw Error("NBT path not found or has a different type", 0);
  return {source, tokens, *found};
}

[[nodiscard]] TokenizedDocument tokenize(std::span<const std::byte> input, const ParseOptions &options = {});
[[nodiscard]] TokenizedView tokenize(std::span<const std::byte> input, std::span<Token> output, const ParseOptions &options = {});
[[nodiscard]] Tag parse(std::span<const std::byte> input, const ParseOptions &options = {});
[[nodiscard]] Tag parse(const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] Tag parse(const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] Tag parse(std::span<const std::byte> input, const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] Tag parse(std::span<const std::byte> input, const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] Buffer serialize(const Tag &root, BinaryFormat format = BinaryFormat::File);

[[nodiscard]] Buffer compress(std::span<const std::byte> input, Compression compression);
[[nodiscard]] Buffer decompress(std::span<const std::byte> input, Compression compression = Compression::Auto);
[[nodiscard]] Tag load(const std::filesystem::path &path, Compression compression = Compression::Auto, const ParseOptions &options = {});
void save(const std::filesystem::path &path, const Tag &root, Compression compression = Compression::None);

[[nodiscard]] Tag clone(const Tag &tag);
using Visitor = std::function<bool(Tag &)>;
using ConstVisitor = std::function<bool(const Tag &)>;
using Predicate = std::function<bool(const Tag &)>;
bool map(Tag &root, const Visitor &visitor);
bool map(const Tag &root, const ConstVisitor &visitor);
[[nodiscard]] std::optional<Tag> filter(const Tag &root, const Predicate &predicate);
void filterInPlace(Tag &root, const Predicate &predicate);
[[nodiscard]] Tag *find(Tag &root, const Predicate &predicate);
[[nodiscard]] const Tag *find(const Tag &root, const Predicate &predicate);
[[nodiscard]] Tag *findByName(Tag &root, std::string_view name);
[[nodiscard]] const Tag *findByName(const Tag &root, std::string_view name);
[[nodiscard]] Tag *findByPath(Tag &root, std::string_view path);
[[nodiscard]] const Tag *findByPath(const Tag &root, std::string_view path);
[[nodiscard]] Tag *at(Tag &container, std::size_t index);
[[nodiscard]] const Tag *at(const Tag &container, std::size_t index);
[[nodiscard]] std::size_t size(const Tag &root);
[[nodiscard]] bool equivalent(const Tag &a, const Tag &b, double epsilon = 1e-6);
[[nodiscard]] Tag parseSnbt(std::string_view input, const ParseOptions &options = {});
[[nodiscard]] std::string toSnbt(const Tag &root, bool pretty = true);
[[nodiscard]] std::string_view typeName(Type type) noexcept;

class Builder {
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
