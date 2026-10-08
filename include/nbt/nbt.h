/**
 * @file nbt.h
 * @author Carlos Machaca (carloscordova96@hotmail.com)
 * @brief Header-only Java Edition NBT codec: structural validation,
 *        materialization into owning tags and big-endian encoding.
 * @version 0.1
 * @date 2026-09-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "nbt/buffer.h"

#ifndef NBT_NS
#define NBT_NS ::nbt::
#endif

namespace nbt {

enum class Type : std::uint8_t;

namespace utils {

constexpr std::size_t MAX_COUNT = static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)());
constexpr std::size_t MAX_STR_SIZE = static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)());
constexpr std::uint32_t NO_NODE = (std::numeric_limits<std::uint32_t>::max)();

template <std::size_t s>
struct BitsType;

template <NBT_NS Type t>
struct TypeTraits;

template <typename T>
struct TypeOf;

template <NBT_NS Type type, NBT_NS Type elementType>
struct UnderlyingType;

} // namespace utils

/**
 * @brief Common base for NBT exceptions. Carries the byte offset where the
 *        problem was detected and an optional nested exception.
 */
class BaseException {
public:
  explicit BaseException(std::size_t offset) : offset_(offset) {
  }

  BaseException(std::size_t offset, const std::exception_ptr &nested) : offset_(offset), nested_(nested) {
  }

  virtual ~BaseException() = default;

  [[nodiscard]] std::size_t offset() const noexcept {
    return offset_;
  }

  [[nodiscard]] const std::exception_ptr &nested() const noexcept {
    return nested_;
  }

private:
  std::size_t offset_;
  std::exception_ptr nested_;
};

/**
 * @brief Fatal NBT error: malformed input, violated limits or an invalid root tag.
 */
class Exception : public NBT_NS BaseException, public std::runtime_error {
public:
  Exception(const std::string &message, std::size_t offset) : NBT_NS BaseException(offset), std::runtime_error(message) {
  }

  Exception(const std::string &message, std::size_t offset, const std::exception_ptr &nested) : NBT_NS BaseException(offset, nested), std::runtime_error(message) {
  }
};

/**
 * @brief Non-fatal truncation signal. Thrown internally when the input ends
 *        before a complete document can be validated; surfaced to callers as
 *        nbt::Status::NeedMoreData.
 */
class NeedMoreDataException : public NBT_NS BaseException, public std::out_of_range {
public:
  NeedMoreDataException(const std::string &message, std::size_t offset) : NBT_NS BaseException(offset), std::out_of_range(message) {
  }

  NeedMoreDataException(const std::string &message, std::size_t offset, const std::exception_ptr &nested) : NBT_NS BaseException(offset, nested), std::out_of_range(message) {
  }
};

/** @brief NBT tag type identifiers matching the Java Edition wire ids. */
enum class Type : std::uint8_t { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

/** @brief Document state after validation. */
enum class Status : std::uint8_t { Empty, Complete, NeedMoreData };

/** @brief Resource limits enforced while validating input. */
struct Options {
  std::size_t maxDepth{512};
  std::size_t maxContainerElements{static_cast<std::size_t>(16U * 1024U * 1024U)};
  std::size_t maxTotalNodes{static_cast<std::size_t>(64U * 1024U * 1024U)};
  std::size_t maxInputBytes{static_cast<std::size_t>(1024U * 1024U * 1024U)};
  bool named{true};
};

/**
 * @brief Owning representation of a single NBT tag.
 *
 * Stores an NBT type, an optional list element type and a payload variant.
 * C++ scalars, strings and containers convert directly through value constructors.
 * A tag whose type is Type::End is empty and cannot be encoded as a root.
 */
class Tag final {
public:
  template <typename T>
  using Array = std::vector<T>;

  using End = std::monostate;
  using Byte = std::int8_t;
  using Short = std::int16_t;
  using Int = std::int32_t;
  using Long = std::int64_t;
  using Float = float;
  using Double = double;
  using String = std::string;
  using List = std::variant<End, Array<Tag>, Array<Byte>, Array<Short>, Array<Int>, Array<Long>, Array<Float>, Array<Double>, Array<String>>;
  using Compound = std::map<String, Tag>;
  using ByteArray = Array<Byte>;
  using IntArray = Array<Int>;
  using LongArray = Array<Long>;

  using Payload = std::variant<End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray>;

  static constexpr std::size_t MAX_DEPTH{512};

  template <typename T>
  static constexpr bool isNBTNumber = std::disjunction_v<std::is_same<T, Byte>, std::is_same<T, Short>, std::is_same<T, Int>, std::is_same<T, Long>, std::is_same<T, Float>, std::is_same<T, Double>>;

  template <typename T>
  static constexpr bool isElementType = std::disjunction_v<std::bool_constant<isNBTNumber<T>>, std::is_same<T, String>, std::is_same<T, Tag>>;

  template <typename T>
  static constexpr bool isNBTType = std::disjunction_v<std::bool_constant<isElementType<T>>, std::is_same<T, End>, std::is_same<T, ByteArray>, std::is_same<T, List>, std::is_same<T, Compound>, std::is_same<T, IntArray>, std::is_same<T, LongArray>>;

  Tag() = default;

  Tag(const NBT_NS Tag &) = default;
  Tag(NBT_NS Tag &&) = default;
  NBT_NS Tag &operator=(const NBT_NS Tag &) = default;
  NBT_NS Tag &operator=(NBT_NS Tag &&) = default;

  template <typename T>
    requires(NBT_NS Tag::isNBTNumber<T>)
  Tag(T value) : type_(NBT_NS utils::TypeOf<T>::value), payload_(value) {
  }

  Tag(std::string_view value) : type_(NBT_NS Type::String), payload_(std::string(value)) {
  }

  template <typename T>
    requires std::disjunction_v<std::is_same<T, NBT_NS Tag::Byte>, std::is_same<T, NBT_NS Tag::Int>, std::is_same<T, NBT_NS Tag::Long>>
  Tag(std::initializer_list<T> elements, bool backwardCompatible = false) : Tag(std::vector<T>(elements), backwardCompatible) {
  }

  template <typename T>
    requires std::disjunction_v<std::is_same<T, NBT_NS Tag::Byte>, std::is_same<T, NBT_NS Tag::Int>, std::is_same<T, NBT_NS Tag::Long>>
  Tag(NBT_NS Tag::Array<T> value, bool backwardCompatible = false) {
    if constexpr (std::is_same_v<T, NBT_NS Tag::Byte>) {
      this->type_ = backwardCompatible ? NBT_NS Type::List : NBT_NS Type::ByteArray;
      this->elementType_ = NBT_NS Type::Byte;
    } else if constexpr (std::is_same_v<T, NBT_NS Tag::Int>) {
      this->type_ = backwardCompatible ? NBT_NS Type::List : NBT_NS Type::IntArray;
      this->elementType_ = NBT_NS Type::Int;
    } else {
      this->type_ = backwardCompatible ? NBT_NS Type::List : NBT_NS Type::LongArray;
      this->elementType_ = NBT_NS Type::Long;
    }
    if (backwardCompatible) {
      this->payload_ = NBT_NS Tag::List(std::move(value));
    } else {
      this->payload_ = std::move(value);
    }
  }

  template <typename T>
    requires(isNBTType<T> && !std::disjunction_v<std::is_same<T, NBT_NS Tag::Byte>, std::is_same<T, NBT_NS Tag::Int>, std::is_same<T, NBT_NS Tag::Long>>)
  Tag(std::initializer_list<T> elements) : Tag(std::vector<T>(elements)) {
  }

  template <typename T>
    requires(isNBTType<T> && !std::disjunction_v<std::is_same<T, NBT_NS Tag::Byte>, std::is_same<T, NBT_NS Tag::Int>, std::is_same<T, NBT_NS Tag::Long>>)
  Tag(NBT_NS Tag::Array<T> elements) {
    if constexpr (std::disjunction_v<std::is_same<T, NBT_NS Tag::Short>, std::is_same<T, NBT_NS Tag::Float>, std::is_same<T, NBT_NS Tag::Double>, std::is_same<T, NBT_NS Tag::String>>) {
      this->type_ = NBT_NS Type::List;
      if constexpr (std::is_same_v<T, NBT_NS Tag::Short>) {
        this->elementType_ = NBT_NS Type::Short;
      } else if constexpr (std::is_same_v<T, NBT_NS Tag::Float>) {
        this->elementType_ = NBT_NS Type::Float;
      } else if constexpr (std::is_same_v<T, NBT_NS Tag::Double>) {
        this->elementType_ = NBT_NS Type::Double;
      } else {
        this->elementType_ = NBT_NS Type::String;
      }
      this->payload_ = NBT_NS Tag::List(std::move(elements));
    } else if constexpr (std::is_same_v<T, NBT_NS Tag>) {
      this->type_ = NBT_NS Type::List;
      this->elementType_ = elements.empty() ? NBT_NS Type::End : elements.front().type_;
      this->payload_ = std::move(elements);
    } else if constexpr (!std::is_same_v<T, End>) {
      NBT_NS Tag::Array<NBT_NS Tag> children;
      children.reserve(elements.size());
      for (auto &element : elements) {
        children.emplace_back(std::move(element));
      }
      this->type_ = NBT_NS Type::List;
      this->elementType_ = children.empty() ? NBT_NS Type::End : children.front().type_;
      this->payload_ = std::move(children);
    }
  }

  Tag(std::initializer_list<std::pair<const NBT_NS Tag::String, NBT_NS Tag>> elements) : Tag(NBT_NS Tag::Compound(elements)) {
  }

  Tag(NBT_NS Tag::Compound elements) : type_(NBT_NS Type::Compound), payload_(std::move(elements)) {
  }

  /** @brief NBT type of this tag. */
  [[nodiscard]] NBT_NS Type type() const noexcept {
    return type_;
  }

  /** @brief Set the NBT type. */
  void type(NBT_NS Type type) noexcept {
    type_ = type;
  }

  /** @brief Element type; only meaningful for List, ByteArray, IntArray, LongArray. */
  [[nodiscard]] NBT_NS Type elementType() const noexcept {
    return elementType_;
  }

  /** @brief Set the element type. */
  void elementType(NBT_NS Type elementType) noexcept {
    elementType_ = elementType;
  }

  /** @brief Owned payload variant matching type(). */
  [[nodiscard]] const NBT_NS Tag::Payload &payload() const noexcept {
    return payload_;
  }

  /** @brief Replace the payload. */
  void payload(NBT_NS Tag::Payload payload) {
    payload_ = std::move(payload);
  }

  /** @brief Find a child by name in a Compound payload. Returns nullptr if not found. */
  [[nodiscard]] NBT_NS Tag *find(std::string_view name) {
    if (std::holds_alternative<NBT_NS Tag::Compound>(payload_)) [[likely]] {
      auto &payload = std::get<NBT_NS Tag::Compound>(payload_);
      if (auto item = payload.find(std::string(name)); item != payload.end()) {
        return &item->second;
      }
    }
    return nullptr;
  }

  /** @brief Append a child to a list or typed-array payload. */
  template <typename T>
    requires(NBT_NS Tag::isNBTType<T>)
  void add(T value) {
    if constexpr (std::is_same_v<T, NBT_NS Tag>) {
      addImpl(std::move(value));
    } else {
      addImpl(NBT_NS Tag(std::move(value)));
    }
  }

  /** @brief Insert or replace a named child in a Compound payload. */
  template <typename T>
    requires(NBT_NS Tag::isNBTType<T>)
  void add(std::string_view name, T value) {
    if constexpr (std::is_same_v<T, NBT_NS Tag>) {
      addImpl(name, std::move(value));
    } else {
      addImpl(name, NBT_NS Tag(std::move(value)));
    }
  }

  /** @brief Number of direct children for container payloads, zero otherwise. */
  [[nodiscard]] std::size_t size() const noexcept {
    switch (type_) {
    case NBT_NS Type::ByteArray:
      return arrayCount<NBT_NS Tag::ByteArray>();
    case NBT_NS Type::IntArray:
      return arrayCount<NBT_NS Tag::IntArray>();
    case NBT_NS Type::LongArray:
      return arrayCount<NBT_NS Tag::LongArray>();
    case NBT_NS Type::Compound:
      return arrayCount<NBT_NS Tag::Compound>();
    case NBT_NS Type::List: {
      const auto *list = std::get_if<NBT_NS Tag::List>(&payload_);
      if (list == nullptr) {
        return 0;
      }
      switch (elementType_) {
      case NBT_NS Type::Byte:
        return listCount<NBT_NS Tag::Array<Byte>>(*list);
      case NBT_NS Type::Short:
        return listCount<NBT_NS Tag::Array<Short>>(*list);
      case NBT_NS Type::Int:
        return listCount<NBT_NS Tag::Array<Int>>(*list);
      case NBT_NS Type::Long:
        return listCount<NBT_NS Tag::Array<Long>>(*list);
      case NBT_NS Type::Float:
        return listCount<NBT_NS Tag::Array<Float>>(*list);
      case NBT_NS Type::Double:
        return listCount<NBT_NS Tag::Array<Double>>(*list);
      case NBT_NS Type::String:
        return listCount<NBT_NS Tag::Array<String>>(*list);
      case NBT_NS Type::End:
        return 0;
      default:
        return listCount<NBT_NS Tag::Array<NBT_NS Tag>>(*list);
      }
    }
    default:
      return 0;
    }
  }

  /** @brief True when the tag is not TAG_End. */
  operator bool() const {
    return type_ != NBT_NS Type::End;
  }

private:
  template <typename Container>
  [[nodiscard]] std::size_t arrayCount() const noexcept {
    const auto *values = std::get_if<Container>(&payload_);
    return values == nullptr ? 0 : values->size();
  }

  template <typename Container>
  [[nodiscard]] static std::size_t listCount(const List &list) noexcept {
    const auto *values = std::get_if<Container>(&list);
    return values == nullptr ? 0 : values->size();
  }

  void addImpl(NBT_NS Tag child) {
    const NBT_NS Type childType = child.type_;
    if (childType == NBT_NS Type::End) {
      return;
    }
    if (this->type_ == NBT_NS Type::End) {
      switch (childType) {
      case NBT_NS Type::Byte: {
        type_ = NBT_NS Type::ByteArray;
        elementType_ = NBT_NS Type::Byte;
        payload_ = NBT_NS Tag::ByteArray();
        break;
      }
      case NBT_NS Type::Short: {
        type_ = NBT_NS Type::List;
        elementType_ = NBT_NS Type::Short;
        payload_ = NBT_NS Tag::List(NBT_NS Tag::Array<NBT_NS Tag::Short>());
        break;
      }
      case NBT_NS Type::Int: {
        type_ = NBT_NS Type::IntArray;
        elementType_ = NBT_NS Type::Int;
        payload_ = NBT_NS Tag::IntArray();
        break;
      }
      case NBT_NS Type::Long: {
        type_ = NBT_NS Type::LongArray;
        elementType_ = NBT_NS Type::Long;
        payload_ = NBT_NS Tag::LongArray();
        break;
      }
      case NBT_NS Type::Float: {
        type_ = NBT_NS Type::List;
        elementType_ = NBT_NS Type::Float;
        payload_ = NBT_NS Tag::List(NBT_NS Tag::Array<NBT_NS Tag::Float>());
        break;
      }
      case NBT_NS Type::Double: {
        type_ = NBT_NS Type::List;
        elementType_ = NBT_NS Type::Double;
        payload_ = NBT_NS Tag::List(NBT_NS Tag::Array<NBT_NS Tag::Double>());
        break;
      }
      case NBT_NS Type::String: {
        type_ = NBT_NS Type::List;
        elementType_ = NBT_NS Type::String;
        payload_ = NBT_NS Tag::List(NBT_NS Tag::Array<NBT_NS Tag::String>());
        break;
      }
      default: {
        type_ = NBT_NS Type::List;
        elementType_ = childType;
        payload_ = NBT_NS Tag::List(NBT_NS Tag::Array<NBT_NS Tag>());
        break;
      }
      }
    }
    switch (type_) {
    case NBT_NS Type::ByteArray: {
      if (childType != NBT_NS Type::Byte) {
        return;
      }
      std::get<NBT_NS Tag::ByteArray>(payload_).push_back(std::get<NBT_NS Tag::Byte>(child.payload_));
      return;
    }
    case NBT_NS Type::IntArray: {
      if (childType != NBT_NS Type::Int) {
        return;
      }
      std::get<NBT_NS Tag::IntArray>(payload_).push_back(std::get<NBT_NS Tag::Int>(child.payload_));
      return;
    }
    case NBT_NS Type::LongArray: {
      if (childType != NBT_NS Type::Long) {
        return;
      }
      std::get<NBT_NS Tag::LongArray>(payload_).push_back(std::get<NBT_NS Tag::Long>(child.payload_));
      return;
    }
    case NBT_NS Type::List:
      if (childType != elementType_) {
        return;
      }
      switch (elementType_) {
      case NBT_NS Type::Short: {
        std::get<NBT_NS Tag::Array<NBT_NS Tag::Short>>(std::get<NBT_NS Tag::List>(payload_)).push_back(std::get<NBT_NS Tag::Short>(child.payload_));
        return;
      }
      case NBT_NS Type::Float: {
        std::get<NBT_NS Tag::Array<NBT_NS Tag::Float>>(std::get<NBT_NS Tag::List>(payload_)).push_back(std::get<NBT_NS Tag::Float>(child.payload_));
        return;
      }
      case NBT_NS Type::Double: {
        std::get<NBT_NS Tag::Array<NBT_NS Tag::Double>>(std::get<NBT_NS Tag::List>(payload_)).push_back(std::get<NBT_NS Tag::Double>(child.payload_));
        return;
      }
      case NBT_NS Type::String: {
        std::get<NBT_NS Tag::Array<NBT_NS Tag::String>>(std::get<NBT_NS Tag::List>(payload_)).push_back(std::get<NBT_NS Tag::String>(child.payload_));
        return;
      }
      default: {
        std::get<NBT_NS Tag::Array<NBT_NS Tag>>(std::get<NBT_NS Tag::List>(payload_)).push_back(std::move(child));
        return;
      }
      }
    default:
      return;
    }
  }

  void addImpl(std::string_view name, Tag child) {
    if (type_ == NBT_NS Type::End) {
      type_ = NBT_NS Type::Compound;
      payload_ = NBT_NS Tag::Compound{};
    }
    if (type_ != NBT_NS Type::Compound || child.type_ == NBT_NS Type::End) {
      return;
    }
    std::get<Compound>(payload_).insert_or_assign(std::string(name), std::move(child));
  }

  NBT_NS Type type_{NBT_NS Type::End};
  NBT_NS Type elementType_{NBT_NS Type::End};
  NBT_NS Tag::Payload payload_;
};

/**
 * @brief Typed-value literals and the @c name|value naming helper.
 *
 * Numeric suffixes: @c _tb, @c _ts, @c _ti, @c _tl, @c _tf, @c _td.
 * String suffix: @c _ts (const char*, size_t).
 * @c "name"|value produces a named pair for Compound initializer lists.
 */
inline namespace tag_literals {

[[nodiscard]] constexpr NBT_NS Tag::Byte operator""_tb(unsigned long long value) noexcept {
  return NBT_NS Tag::Byte(value);
}

[[nodiscard]] constexpr NBT_NS Tag::Short operator""_ts(unsigned long long value) noexcept {
  return NBT_NS Tag::Short(value);
}

[[nodiscard]] constexpr NBT_NS Tag::Int operator""_ti(unsigned long long value) noexcept {
  return NBT_NS Tag::Int(value);
}

[[nodiscard]] constexpr NBT_NS Tag::Long operator""_tl(unsigned long long value) noexcept {
  return NBT_NS Tag::Long(value);
}

[[nodiscard]] constexpr NBT_NS Tag::Float operator""_tf(long double value) noexcept {
  return NBT_NS Tag::Float(value);
}

[[nodiscard]] constexpr NBT_NS Tag::Double operator""_td(long double value) noexcept {
  return NBT_NS Tag::Double(value);
}

[[nodiscard]] NBT_NS Tag::String operator""_ts(const char *str, size_t len) {
  return NBT_NS Tag::String(str, len);
}

[[nodiscard]] inline std::pair<const std::string, NBT_NS Tag> operator|(std::string_view name, const NBT_NS Tag &value) {
  return std::pair<const std::string, NBT_NS Tag>{name, value};
}

[[nodiscard]] inline std::pair<const std::string, NBT_NS Tag> operator|(std::string_view name, NBT_NS Tag &&value) {
  return std::pair<const std::string, NBT_NS Tag>{name, std::move(value)};
}

template <typename Value>
  requires(!std::is_same_v<std::remove_cvref_t<Value>, NBT_NS Tag> && std::is_constructible_v<NBT_NS Tag, Value &&>)
[[nodiscard]] inline std::pair<const std::string, NBT_NS Tag> operator|(std::string_view name, Value &&value) {
  return name | NBT_NS Tag(std::forward<Value>(value));
}

} // namespace tag_literals

/** @brief Structural index entry: byte offsets and child/sibling links. */
struct Node {
  static constexpr std::uint32_t END = (std::numeric_limits<std::uint32_t>::max)();

  std::uint32_t begin{NBT_NS Node::END};   ///< Offset of the type byte (named) or payload (list element).
  std::uint32_t end{NBT_NS Node::END};     ///< One past the last byte of this tag.
  std::uint32_t payload{NBT_NS Node::END}; ///< Offset of the decodable payload.

  std::uint32_t firstChild{NBT_NS utils::NO_NODE};  ///< Index of the first child node.
  std::uint32_t nextSibling{NBT_NS utils::NO_NODE}; ///< Index of the next sibling node.

  std::uint32_t childCount{0}; ///< Number of direct children.

  NBT_NS Type type{NBT_NS Type::End};        ///< NBT type of the node.
  NBT_NS Type elementType{NBT_NS Type::End}; ///< List element type when type is Type::List.

  explicit Node(std::uint32_t begin) : begin(begin) {
  }
};

class NbtUtilities;

/**
 * @brief NBT document: validates input, exposes non-owning views, materializes
 *        owning tags and encodes them back to binary.
 *
 * Parsing indexes the complete structure without decoding values. Truncated
 * input produces Status::NeedMoreData and can be completed with append().
 * Malformed input throws Exception.
 */
class NbtParser final {

private:
  struct Lifetime {
    const NbtParser *document;
  };

public:
  /** @brief Parse borrowed bytes; truncated input yields Status::NeedMoreData. */
  [[nodiscard]] static NBT_NS NbtParser parseAtMost(std::span<const std::byte> data, const NBT_NS Options &options = NBT_NS Options{}) {
    return parseImpl(data, options, true);
  }

  /** @brief Parse borrowed bytes; truncated input throws NeedMoreDataException. */
  [[nodiscard]] static NBT_NS NbtParser parse(std::span<const std::byte> data, const NBT_NS Options &options = NBT_NS Options{}) {
    return parseImpl(data, options, false);
  }

  /** @brief Parse a contiguous container (copies bytes); truncated input yields Status::NeedMoreData. */
  template <typename Container>
  [[nodiscard]] static NBT_NS NbtParser parseAtMost(const Container &data, const NBT_NS Options &options = NBT_NS Options{}) {
    return parseImpl(data, options, true);
  }

  /** @brief Parse a contiguous container (copies bytes); truncated input throws NeedMoreDataException. */
  template <typename Container>
  [[nodiscard]] static NBT_NS NbtParser parse(const Container &data, const NBT_NS Options &options = NBT_NS Options{}) {
    return parseImpl(data, options, false);
  }

  /** @brief Non-owning lazy view over a validated node in the document. */
  class TagView {
  protected:
    enum class AccessType : std::uint8_t {
      Node,
      SubItem,
    };

  public:
    class Iterator {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = TagView;
      using difference_type = std::ptrdiff_t;
      using reference = TagView;
      using pointer = void;

      Iterator() = default;

      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      Iterator(std::weak_ptr<NBT_NS NbtParser::Lifetime> owner, std::uint32_t index, std::uint32_t position) : owner_(std::move(owner)), index_(index), position_(position), accessType_(NBT_NS NbtParser::TagView::AccessType::SubItem) {
      }

      Iterator(std::weak_ptr<NBT_NS NbtParser::Lifetime> owner, std::uint32_t index) : owner_(std::move(owner)), index_(index) {
      }

      [[nodiscard]] reference operator*() const {
        auto owner = owner_.lock();

        if (accessType_ == NBT_NS NbtParser::TagView::AccessType::Node) {
          if (owner == nullptr || index_ == NBT_NS utils::NO_NODE || index_ >= owner->document->nodes_.size()) {
            throw NBT_NS Exception("Document not found", index_);
          }
          return NBT_NS NbtParser::TagView{owner->document, index_};
        }

        if (owner == nullptr || index_ == NBT_NS utils::NO_NODE || position_ == NBT_NS utils::NO_NODE || position_ >= owner->document->nodes_[index_].childCount) {
          throw NBT_NS Exception("Document not found", index_);
        }
        return NBT_NS NbtParser::TagView{owner->document, index_, position_};
      }

      Iterator &operator++() {
        auto owner = owner_.lock();

        if (accessType_ == NBT_NS NbtParser::TagView::AccessType::Node) {
          if (owner != nullptr && index_ != NBT_NS utils::NO_NODE && index_ < owner->document->nodes_.size()) {
            index_ = owner->document->nodes_[index_].nextSibling;
          } else {
            index_ = NBT_NS utils::NO_NODE;
          }
          return *this;
        }

        if (owner != nullptr && index_ != NBT_NS utils::NO_NODE && position_ != NBT_NS utils::NO_NODE && index_ < owner->document->nodes_.size() && position_ < owner->document->nodes_[index_].childCount) {
          position_++;
          if (position_ < owner->document->nodes_[index_].childCount) {
            return *this;
          }
        }

        index_ = NBT_NS utils::NO_NODE;
        position_ = NBT_NS utils::NO_NODE;
        return *this;
      }

      Iterator operator++(int) {
        Iterator tmp = *this;
        ++(*this);
        return tmp;
      }

      [[nodiscard]] bool operator==(const Iterator &other) const noexcept {
        if (index_ == other.index_ && index_ == NBT_NS utils::NO_NODE) {
          return true;
        }
        const bool sameOwner = !owner_.owner_before(other.owner_) && !other.owner_.owner_before(owner_);
        return sameOwner && accessType_ == other.accessType_ && index_ == other.index_ && position_ == other.position_;
      }

    private:
      std::weak_ptr<NBT_NS NbtParser::Lifetime> owner_;
      std::uint32_t index_{NBT_NS utils::NO_NODE};
      std::uint32_t position_{NBT_NS utils::NO_NODE};
      NBT_NS NbtParser::TagView::AccessType accessType_{AccessType::Node};
    };

    TagView() = default;

    /** @brief Element type for arrays and lists, Type::End otherwise. */
    [[nodiscard]] NBT_NS Type elementType() const {
      auto owner = owner_.lock();
      if (owner == nullptr || accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem) {
        return NBT_NS Type::End;
      }

      const auto &entry = node(owner);
      switch (entry.type) {
      case NBT_NS Type::ByteArray:
        return NBT_NS Type::Byte;
      case NBT_NS Type::IntArray:
        return NBT_NS Type::Int;
      case NBT_NS Type::LongArray:
        return NBT_NS Type::Long;
      case NBT_NS Type::List:
        return entry.elementType;
      default:
        return NBT_NS Type::End;
      }
    }

    /** @brief Number of direct children (containers only). */
    [[nodiscard]] std::size_t size() const {
      auto owner = owner_.lock();
      return owner == nullptr || accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem ? 0 : node(owner).childCount;
    }

    /** @brief Whether the container has no children. */
    [[nodiscard]] bool empty() const {
      return size() == 0;
    }

    /** @brief Tag name; empty for unnamed tags such as list elements. */
    [[nodiscard]] std::string_view name() const {
      auto owner = owner_.lock();
      if (owner == nullptr || accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem) {
        return "";
      }
      return owner->document->readName(node(owner));
    }

    /** @brief NBT type of the referenced tag. */
    [[nodiscard]] NBT_NS Type type() const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        return NBT_NS Type::End;
      }
      const auto &entry = node(owner);
      return accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem ? entry.elementType : entry.type;
    }

    /** @brief Access the child at @p position. Returns an empty view if out of range. */
    [[nodiscard]] NBT_NS NbtParser::TagView child(std::size_t position) const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        return {};
      }

      const auto &entry = node(owner);
      if (position >= entry.childCount) {
        return {};
      }

      switch (entry.type) {
      case NBT_NS Type::ByteArray:
      case NBT_NS Type::IntArray:
      case NBT_NS Type::LongArray:
      case NBT_NS Type::List: {
        switch (entry.elementType) {
        case NBT_NS Type::Byte:
        case NBT_NS Type::Short:
        case NBT_NS Type::Int:
        case NBT_NS Type::Long:
        case NBT_NS Type::Float:
        case NBT_NS Type::Double:
        case NBT_NS Type::String: {
          return {owner->document, index_, static_cast<std::uint32_t>(position)};
        }
        default:
          break;
        }
      }
      default:
        break;
      }

      std::uint32_t current = entry.firstChild;
      for (std::size_t index = 0; index < position; ++index) {
        current = owner->document->nodes_[current].nextSibling;
      }
      return {owner->document, current};
    }

    /** @brief Find a child by name inside a Compound. Returns an empty view if not found. */
    [[nodiscard]] TagView find(std::string_view requestedName) const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        return {};
      }

      const auto &entry = node(owner);
      std::uint32_t current = entry.firstChild;
      for (std::size_t index = 0; index < entry.childCount; ++index) {
        if (owner->document->readName(owner->document->nodes_[current]) == requestedName) {
          return TagView{owner->document, current};
        }
        current = owner->document->nodes_[current].nextSibling;
      }
      return {};
    }

    [[nodiscard]] TagView operator[](std::size_t position) const {
      return child(position);
    }

    [[nodiscard]] TagView operator[](std::string_view requestedName) const {
      auto result = find(requestedName);
      if (!result) {
        throw Exception("child not found", index_);
      }
      return result;
    }

    /** @brief Decode the payload as the given NBT type. */
    template <NBT_NS Type t>
    [[nodiscard]] auto as() const {
      if constexpr (t == NBT_NS Type::End) {
        static_assert(t != NBT_NS Type::End, "as<T>() cannot decode TAG_End");
        throw std::bad_variant_access();
      } else if constexpr (NBT_NS utils::TypeTraits<t>::isScalar) {
        return decodeNumber<typename NBT_NS utils::TypeTraits<t>::value_t>(t);
      } else if constexpr (t == NBT_NS Type::String) {
        return decodeString(t);
      } else if constexpr (NBT_NS utils::TypeTraits<t>::isArray) {
        return decodeArray<typename NBT_NS utils::TypeTraits<t>::element_t>(t);
      } else if constexpr (t == NBT_NS Type::List) {
        return decodeList();
      } else if constexpr (t == NBT_NS Type::Compound) {
        return decodeCompound();
      } else {
        throw std::bad_variant_access();
      }
    }

    /** @brief Materialize this view into an owning Tag tree. */
    [[nodiscard]] operator Tag() const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        throw Exception("Document not found", index_);
      }
      if (accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem) {
        const auto &entry = node(owner);
        switch (entry.elementType) {
        case NBT_NS Type::Byte:
          return NBT_NS Tag(owner->document->readNumber<NBT_NS Tag::Byte>(entry, position_));
        case NBT_NS Type::Short:
          return NBT_NS Tag(owner->document->readNumber<NBT_NS Tag::Short>(entry, position_));
        case NBT_NS Type::Int:
          return NBT_NS Tag(owner->document->readNumber<NBT_NS Tag::Int>(entry, position_));
        case NBT_NS Type::Long:
          return NBT_NS Tag(owner->document->readNumber<NBT_NS Tag::Long>(entry, position_));
        case NBT_NS Type::Float:
          return NBT_NS Tag(owner->document->readNumber<NBT_NS Tag::Float>(entry, position_));
        case NBT_NS Type::Double:
          return NBT_NS Tag(owner->document->readNumber<NBT_NS Tag::Double>(entry, position_));
        case NBT_NS Type::String:
          return NBT_NS Tag(owner->document->readString(entry, position_));
        default:
          throw std::bad_variant_access();
        }
      }
      return owner->document->readTag(index_);
    }

    /** @brief Whether the view references a valid node. */
    [[nodiscard]] explicit operator bool() const noexcept {
      return !owner_.expired();
    }

    /** @brief Iterator to the first child. */
    [[nodiscard]] Iterator begin() const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        return {};
      }

      if (accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem) {
        return {};
      }

      const auto &entry = node(owner);
      switch (entry.elementType) {
      case NBT_NS Type::Byte:
      case NBT_NS Type::Short:
      case NBT_NS Type::Int:
      case NBT_NS Type::Long:
      case NBT_NS Type::Float:
      case NBT_NS Type::Double:
      case NBT_NS Type::String: {
        return {owner_, index_, 0};
      }
      default:
        return {owner_, entry.firstChild};
      }
    }

    /** @brief Sentinel past the last child. */
    [[nodiscard]] Iterator end() const {
      return {};
    }

  private:
    friend class NBT_NS NbtParser;

    using Lifetime = std::shared_ptr<NBT_NS NbtParser::Lifetime>;

    TagView(const NBT_NS NbtParser *owner, std::uint32_t index) : owner_(owner->lifetime_), index_(index) {
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    TagView(const NBT_NS NbtParser *owner, std::uint32_t index, std::uint32_t position) : owner_(owner->lifetime_), index_(index), position_(position), accessType_(NBT_NS NbtParser::TagView::AccessType::SubItem) {
    }

    [[nodiscard]] const NBT_NS Node &node(const Lifetime &owner) const {
      if (owner == nullptr || index_ >= owner->document->nodes_.size()) {
        throw std::logic_error("invalid NBT view");
      }
      return owner->document->nodes_[index_];
    }

    [[nodiscard]] const NBT_NS Node &node(const Lifetime &owner, std::uint32_t index) const {
      if (owner == nullptr || index >= owner->document->nodes_.size()) {
        throw std::logic_error("invalid NBT view");
      }
      return owner->document->nodes_[index];
    }

    template <typename T>
    T decodeNumber(Type expected) const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        throw Exception("Document not found", index_);
      }
      const auto &entry = node(owner);
      const auto type = accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem ? entry.elementType : entry.type;
      if (type != expected) {
        throw std::bad_variant_access();
      }
      if (accessType_ == NBT_NS NbtParser::TagView::AccessType::Node) {
        return owner->document->readNumber<T>(entry);
      }
      return owner->document->readNumber<T>(entry, position_);
    }

    [[nodiscard]] std::string_view decodeString(Type expected) const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        throw Exception("Document not found", index_);
      }
      const auto &entry = node(owner);
      const auto type = accessType_ == NBT_NS NbtParser::TagView::AccessType::SubItem ? entry.elementType : entry.type;
      if (type != expected) {
        throw std::bad_variant_access();
      }
      if (accessType_ == NBT_NS NbtParser::TagView::AccessType::Node) {
        return owner->document->readString(entry);
      }
      return owner->document->readString(entry, position_);
    }

    template <typename T>
    [[nodiscard]] NBT_NS Tag::Array<T> decodeArray(Type expected) const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        throw Exception("Document not found", index_);
      }
      const auto &entry = node(owner);
      if (entry.type != expected) {
        throw std::bad_variant_access();
      }
      return owner->document->readArray<T>(entry);
    }

    [[nodiscard]] std::vector<TagView> decodeList() const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        throw Exception("Document not found", index_);
      }
      const auto &entry = node(owner);
      if (entry.type != NBT_NS Type::List) {
        throw std::bad_variant_access();
      }
      auto childCount = entry.childCount;
      std::vector<TagView> values;
      values.reserve(static_cast<std::size_t>(childCount));
      std::uint32_t childIndex = entry.firstChild;
      for (std::uint32_t index = 0; index < childCount; index++) {
        switch (entry.elementType) {
        case NBT_NS Type::Byte:
        case NBT_NS Type::Short:
        case NBT_NS Type::Int:
        case NBT_NS Type::Long:
        case NBT_NS Type::Float:
        case NBT_NS Type::Double:
        case NBT_NS Type::String:
          values.emplace_back(TagView(owner->document, index_, index));
          break;
        default:
          values.emplace_back(TagView(owner->document, childIndex));
          childIndex = node(owner, childIndex).nextSibling;
          break;
        }
      }
      return values;
    }

    [[nodiscard]] std::map<std::string, TagView> decodeCompound() const {
      auto owner = owner_.lock();
      if (owner == nullptr) {
        throw Exception("Document not found", index_);
      }
      const auto &entry = node(owner);
      if (entry.type != NBT_NS Type::Compound) {
        throw std::bad_variant_access();
      }
      auto childCount = entry.childCount;
      std::map<std::string, TagView> values;
      std::uint32_t childIndex = entry.firstChild;
      for (std::uint32_t index = 0; index < childCount; index++) {
        const auto &subEntry = node(owner, childIndex);
        values.insert_or_assign(NBT_NS Tag::String(owner->document->readName(subEntry)), NBT_NS NbtParser::TagView(owner->document, childIndex));
        childIndex = subEntry.nextSibling;
      }
      return values;
    }

    std::weak_ptr<NBT_NS NbtParser::Lifetime> owner_;
    std::uint32_t index_{NBT_NS utils::NO_NODE};
    std::uint32_t position_{NBT_NS utils::NO_NODE};
    NBT_NS NbtParser::TagView::AccessType accessType_{AccessType::Node};
  };

  NbtParser() = default;

  NbtParser(const NbtParser &) = delete;

  NbtParser &operator=(const NbtParser &) = delete;

  NbtParser(NbtParser &&other) noexcept {
    this->swap(other);
  }

  NbtParser &operator=(NbtParser &&other) noexcept {
    this->swap(other);
    return *this;
  }

  /** @brief Construct a document from an already-materialized root tag, ready for encode(). */
  explicit NbtParser(NBT_NS Tag rootValue) {
    rootValue_ = std::move(rootValue);
    status_ = NBT_NS Status::Complete;
  }

  /** @brief Exchange the full state with another document. */
  void swap(NbtParser &nbt) noexcept {
    using std::swap;

    data_.swap(nbt.data_);
    swap(status_, nbt.status_);

    swap(rootValue_, nbt.rootValue_);
    swap(nodes_, nbt.nodes_);

    swap(position_, nbt.position_);
    swap(next_node_, nbt.next_node_);

    swap(maxDepth_, nbt.maxDepth_);
    swap(maxContainerElements_, nbt.maxContainerElements_);
    swap(maxTotalNodes_, nbt.maxTotalNodes_);
    swap(maxInputBytes_, nbt.maxInputBytes_);
    swap(named_, nbt.named_);

    swap(lifetime_, nbt.lifetime_);
    if (lifetime_ != nullptr) {
      lifetime_->document = this;
    }
    if (nbt.lifetime_ != nullptr) {
      nbt.lifetime_->document = &nbt;
    }
  }

  /**
   * @brief Append a byte fragment and revalidate accumulated input.
   *
   * Empty chunks are a no-op. Truncated input stays at Status::NeedMoreData.
   * @throws Exception if the accumulated input is malformed or exceeds a limit.
   */
  template <typename Container>
  void append(const Container &chunk) {
    append(asBytes(chunk));
  }

  /// @copydoc append
  void append(std::span<const std::byte> chunk) {
    if (chunk.empty()) {
      return;
    }
    appendImpl(chunk);
    validate(true);
  }

  /** @brief Reset to Status::Empty, discarding bytes, index and root tag. */
  void clear() noexcept {
    data_.reset();
    rootValue_.reset();
    status_ = NBT_NS Status::Empty;
    position_ = 0;
    next_node_ = 0;
    nodes_.clear();
    named_ = false;
  }

  /** @brief Current document state. */
  [[nodiscard]] NBT_NS Status status() const noexcept {
    return status_;
  }

  /** @brief Whether the input validated completely. */
  [[nodiscard]] bool valid() const noexcept {
    return status_ == NBT_NS Status::Complete;
  }

  /** @brief Whether the document owns its bytes rather than borrowing a span. */
  [[nodiscard]] bool ownsBytes() const noexcept {
    return data_.capacity() > 0;
  }

  /** @brief Current internal or borrowed input bytes. */
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {data_.data(), data_.size()};
  }

  /**
   * @brief Obtain a lazy root view over the validated bytes.
   * @throws std::logic_error if the document is incomplete.
   */
  [[nodiscard]] TagView root() const {
    if (status_ != NBT_NS Status::Complete || nodes_.empty()) {
      throw std::logic_error("NBT is incomplete");
    }
    return {this, 0};
  }

  /**
   * @brief Materialize the root into an owning Tag tree.
   * @throws std::logic_error if the document is incomplete.
   */
  [[nodiscard]] NBT_NS Tag readTag() const {
    if (rootValue_) {
      return *rootValue_;
    }
    if (status_ != NBT_NS Status::Complete || nodes_.empty()) {
      throw std::logic_error("NBT is incomplete");
    }
    return readTag(0);
  }

  /**
   * @brief Encode the document into a new Buffer.
   * @param name Optional root name; std::nullopt omits the name header.
   */
  [[nodiscard]] NBT_NS Buffer encode(std::optional<std::string_view> name = std::nullopt) const {
    NBT_NS Buffer output;
    encode(output, name);
    return output;
  }

  /** @brief Encode the document, appending to an existing Buffer. */
  void encode(NBT_NS Buffer &output, std::optional<std::string_view> name = std::nullopt) const {
    if (status_ != NBT_NS Status::Complete) {
      throw NBT_NS Exception("incomplete data", 0);
    }

    if (rootValue_) {
      if (rootValue_->type() == NBT_NS Type::End) {
        throw std::invalid_argument("TAG_End");
      }

      const std::size_t valueEncodedSize = tagSize({}, rootValue_.value(), name);
      auto [buffer, available] = output.preallocate(valueEncodedSize, BufferUtils::growthSize(valueEncodedSize));
      if (buffer == nullptr) [[unlikely]] {
        throw std::bad_alloc();
      }

      BufferWriter appender{static_cast<std::byte *>(buffer), available};
      appender.writeBE(static_cast<std::uint8_t>(rootValue_->type()));
      if (name) {
        appendString(appender, name.value());
      }
      appendPayload(appender, rootValue_.value());

      output.postallocate(appender.written());
    } else if (!nodes_.empty()) {
      const NBT_NS Node &rootNode = nodes_[0];
      if (rootNode.type == NBT_NS Type::End) {
        throw std::invalid_argument("TAG_End");
      }

      if (name && name->size() > NBT_NS utils::MAX_STR_SIZE) {
        throw std::length_error("NBT string exceeds 65535 bytes");
      }
      const std::size_t headerSize = name ? 2 + name->size() : 0;
      const std::size_t payloadSize = rootNode.end - rootNode.payload;
      const std::size_t valueEncodedSize = 1 + headerSize + payloadSize;
      auto [buffer, available] = output.preallocate(valueEncodedSize, NBT_NS BufferUtils::growthSize(valueEncodedSize));
      if (buffer == nullptr) [[unlikely]] {
        throw std::bad_alloc();
      }

      BufferWriter appender{static_cast<std::byte *>(buffer), available};
      appender.put(static_cast<std::byte>(rootNode.type));
      if (name) {
        appendString(appender, *name);
      }
      appender.write(data_.data() + rootNode.payload, payloadSize);

      output.postallocate(appender.written());
    }
  }

  /**
   * @brief Estimated encoded size in bytes for @p value. Never throws.
   *
   * When @p parent is Compound or End a type-id header (and optional name) is
   * included; list children are headerless payloads. Missing payloads are
   * measured as zero-values, empty strings or empty containers.
   *
   * @param parent The container that will hold @p value (End for root).
   * @param value  Tag to measure.
   * @param name   Root name; std::nullopt omits the name header.
   */
  [[nodiscard]] static std::size_t tagSize(const NBT_NS Tag &parent, const NBT_NS Tag &value, std::optional<std::string_view> name = std::nullopt) noexcept {
    std::size_t header = 0;
    if (parent.type() == NBT_NS Type::Compound || parent.type() == NBT_NS Type::End) {
      header += 1;
      header += name ? 2 + name->size() : 0;
    }
    switch (value.type()) {
    case NBT_NS Type::Byte:
      return header + 1;
    case NBT_NS Type::Short:
      return header + 2;
    case NBT_NS Type::Int:
    case NBT_NS Type::Float:
      return header + 4;
    case NBT_NS Type::Long:
    case NBT_NS Type::Double:
      return header + 8;
    case NBT_NS Type::String:
      return header + stringSize(value);
    case NBT_NS Type::ByteArray:
      return header + arraySize<NBT_NS Tag::Byte>(value);
    case NBT_NS Type::IntArray:
      return header + arraySize<NBT_NS Tag::Int>(value);
    case NBT_NS Type::LongArray:
      return header + arraySize<NBT_NS Tag::Long>(value);
    case NBT_NS Type::List:
      return header + listSize(value);
    case NBT_NS Type::Compound:
      return header + compoundSize(value);
    case NBT_NS Type::End:
    default:
      return header;
    }
  }

private:
  class BufferWriter final {
  public:
    BufferWriter(std::byte *data, std::size_t capacity) : begin_(data), current_(data), end_(data + capacity) {
      if (data == nullptr && capacity != 0) [[unlikely]] {
        throw std::invalid_argument("null buffer writer storage");
      }
    }

    void put(std::byte value) {
      if (current_ == end_) [[unlikely]] {
        throw std::overflow_error("overflow");
      }
      *current_++ = value;
    }

    void write(const void *data, std::size_t length) {
      if (length == 0) {
        return;
      }
      if (data == nullptr) [[unlikely]] {
        throw std::invalid_argument("null buffer writer input");
      }
      if (current_ == nullptr || static_cast<std::size_t>(end_ - current_) < length) [[unlikely]] {
        throw std::overflow_error("overflow");
      }
      std::memcpy(current_, data, length);
      current_ += length;
    }

    template <typename T>
    void writeBE(T value) {
      if constexpr (sizeof(T) == 1) {
        put(static_cast<std::byte>(static_cast<std::uint8_t>(value)));
      } else {
        auto bits = bitCast<T, typename NBT_NS utils::BitsType<sizeof(T)>::bits>(value);
#ifndef NBT_BIG_ENDIAN
        bits = std::byteswap(bits);
#endif
        write(&bits, sizeof(T));
      }
    }

    [[nodiscard]] std::size_t written() const noexcept {
      return begin_ == nullptr ? 0 : static_cast<std::size_t>(current_ - begin_);
    }

  private:
    std::byte *begin_{};
    std::byte *current_{};
    std::byte *end_{};
  };

  friend class NbtUtilities;

  explicit NbtParser(std::span<const std::byte> buffer) : data_{buffer} {
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  std::uint32_t parseNode(std::size_t depth, std::uint32_t previousSibling, bool named = true, NBT_NS Type declared = NBT_NS Type::End) {
    const auto begin = position_;
    const auto nodeIndex = beginNode(begin);
    if (nodes_[nodeIndex].end != NBT_NS Node::END) {
      NBT_NS Node &node = nodes_[nodeIndex];
      position_ = node.end;
      next_node_ = node.nextSibling;
      return nodeIndex;
    }

    if (previousSibling != NBT_NS utils::NO_NODE) {
      nodes_[previousSibling].nextSibling = nodeIndex;
    }

    const auto type = declared == NBT_NS Type::End ? readType() : declared;

    if (named) {
      const auto nameSize = readNumber<std::uint16_t>();
      skip(nameSize);
    }

    parsePayload(nodeIndex, type, depth);
    return nodeIndex;
  }

  [[nodiscard]] std::string_view readName(const NBT_NS Node &value) const {
    if (value.end == NBT_NS Node::END || value.payload <= value.begin + 3) {
      return {};
    }
    auto position = static_cast<std::size_t>(value.begin);
    position = skip(position, 1);
    const auto size = static_cast<std::size_t>(readNumber<std::uint16_t>(position));
    position = skip(position, 2);
    return text(position, size);
  }

  void appendImpl(std::span<const std::byte> chunk) {
    if (rootValue_) {
      throw std::logic_error("cannot append to a document built from a materialized tag");
    }

    if (status_ != NBT_NS Status::NeedMoreData) {
      status_ = NBT_NS Status::NeedMoreData;
    }

    data_.append(chunk.data(), chunk.data() + chunk.size_bytes());
  }

  void validate(bool atMost) {
    position_ = 0;
    next_node_ = 0;
    try {
      if (data_.size() > maxInputBytes_) {
        throw Exception("NBT input byte limit exceeded", 0);
      }

      const auto type = readType(position_);
      if (type == NBT_NS Type::End) {
        throw Exception("unexpected TAG_End", position_);
      }

      parseNode(0, NBT_NS utils::NO_NODE, named_);

      status_ = NBT_NS Status::Complete;
    } catch (const NBT_NS NeedMoreDataException &) {
      next_node_ = 0;
      position_ = 0;
      status_ = NBT_NS Status::NeedMoreData;
      if (atMost) {
        return;
      }
      throw;
    }
  }

  void parsePayload(std::uint32_t nodeIndex, NBT_NS Type type, std::size_t depth) {
    if (depth > maxDepth_) {
      throw Exception("NBT depth limit exceeded", position_);
    }
    auto payload = checkedOffset(position_);
    switch (type) {
    case NBT_NS Type::Byte:
      skip(1);
      break;
    case NBT_NS Type::Short:
      skip(2);
      break;
    case NBT_NS Type::Int:
    case NBT_NS Type::Float:
      skip(4);
      break;
    case NBT_NS Type::Long:
    case NBT_NS Type::Double:
      skip(8);
      break;
    case NBT_NS Type::String:
      skip(readNumber<std::uint16_t>());
      break;
    case NBT_NS Type::ByteArray:
      parseArray<NBT_NS Tag::Byte>(nodeIndex);
      payload += 4;
      break;
    case NBT_NS Type::IntArray:
      parseArray<NBT_NS Tag::Int>(nodeIndex);
      payload += 4;
      break;
    case NBT_NS Type::LongArray:
      parseArray<NBT_NS Tag::Long>(nodeIndex);
      payload += 4;
      break;
    case NBT_NS Type::List:
      parseList(nodeIndex, depth);
      payload += 5;
      break;
    case NBT_NS Type::Compound:
      parseCompound(nodeIndex, depth);
      break;
    case NBT_NS Type::End:
      throw Exception("unexpected TAG_End", position_);
    }
    auto &node = nodes_[nodeIndex];
    node.type = type;
    node.payload = payload;
    node.end = checkedOffset(position_);
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void parseList(std::uint32_t nodeIndex, std::size_t depth) {
    const auto elementType = readType();
    const auto childCount = readCount();
    const auto firstChild = next_node_;

    if (elementType == NBT_NS Type::End && childCount != 0) {
      throw Exception("non-empty TAG_List uses TAG_End", position_);
    }

    auto &node = nodes_[nodeIndex];
    node.elementType = elementType;
    node.childCount = checkedOffset(childCount);

    switch (elementType) {
    case NBT_NS Type::End:
    case NBT_NS Type::Byte: {
      skip(childCount);
      return;
    }
    case NBT_NS Type::Short: {
      skip(childCount * 2);
      return;
    }
    case NBT_NS Type::Int:
    case NBT_NS Type::Float: {
      skip(childCount * 4);
      return;
    }
    case NBT_NS Type::Long:
    case NBT_NS Type::Double: {
      skip(childCount * 8);
      return;
    }
    case NBT_NS Type::String: {
      for (std::size_t index = 0; index < childCount; index++) {
        const auto size = static_cast<std::size_t>(readNumber<std::uint16_t>());
        skip(size);
      }
      return;
    }
    default: {
      node.firstChild = childCount == 0 ? NBT_NS utils::NO_NODE : checkedOffset(firstChild);

      std::uint32_t previousSibling = NBT_NS utils::NO_NODE;
      for (std::size_t index = 0; index < childCount; ++index) {
        previousSibling = parseNode(depth + 1, previousSibling, false, elementType);
      }
      return;
    }
    }
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void parseCompound(std::uint32_t nodeIndex, std::size_t depth) {
    auto firstChild = NBT_NS utils::NO_NODE;
    auto previousSibling = NBT_NS utils::NO_NODE;
    std::size_t count{};
    while (peek() != std::byte{}) {
      if (count >= maxContainerElements_) {
        throw Exception("NBT container element limit exceeded", position_);
      }
      previousSibling = parseNode(depth + 1, previousSibling);
      if (firstChild == NBT_NS utils::NO_NODE) {
        firstChild = previousSibling;
      }
      ++count;
    }
    skip(1);
    auto &node = nodes_[nodeIndex];
    node.firstChild = firstChild;
    node.childCount = checkedOffset(count);
  }

  [[nodiscard]] std::uint32_t beginNode(std::size_t begin) {
    if (next_node_ < nodes_.size()) {
      const auto currentNode = static_cast<std::uint32_t>(next_node_);
      ++next_node_;
      return currentNode;
    }

    if (nodes_.size() >= maxTotalNodes_) {
      throw Exception("NBT node limit exceeded", position_);
    }

    const auto index = checkedOffset(nodes_.size());
    nodes_.emplace_back(checkedOffset(begin));
    next_node_ = nodes_.size();
    return index;
  }

  template <typename T>
  void parseArray(std::uint32_t nodeIndex) {
    const auto count = readCount();
    nodes_[nodeIndex].childCount = checkedOffset(count);
    nodes_[nodeIndex].elementType = NBT_NS utils::TypeOf<T>::value;
    skip(count * sizeof(T));
  }

  [[nodiscard]] std::size_t readCount() {
    const auto count = readNumber<std::uint32_t>();
    if (static_cast<std::size_t>(count) > maxContainerElements_) {
      throw NBT_NS Exception("NBT container element limit exceeded", position_ - 4);
    }
    return static_cast<std::size_t>(count);
  }

  [[nodiscard]] NBT_NS Type readType(std::size_t offset) const {
    const auto raw = readNumber<std::uint8_t>(offset);
    if (raw > static_cast<std::uint8_t>(NBT_NS Type::LongArray)) {
      throw Exception("unknown NBT type", offset);
    }
    return static_cast<Type>(raw);
  }

  [[nodiscard]] NBT_NS Type readType() {
    const auto raw = readNumber<std::uint8_t>();
    if (raw > static_cast<std::uint8_t>(NBT_NS Type::LongArray)) {
      throw Exception("unknown NBT type", position_ - 1);
    }
    return static_cast<Type>(raw);
  }

  template <class T>
  [[nodiscard]] T readNumber(std::size_t offset) const {
    require(offset, sizeof(T));
    if constexpr (sizeof(T) == 1) {
      return static_cast<T>(std::to_integer<std::uint8_t>(data_.data()[offset]));
    } else {
      typename NBT_NS utils::BitsType<sizeof(T)>::bits bits;
      std::memcpy(&bits, data_.data() + offset, sizeof(T));
#ifndef NBT_BIG_ENDIAN
      bits = std::byteswap(bits);
#endif
      return bitCast<T>(bits);
    }
  }

  template <class T>
  [[nodiscard]] T readNumber(const NBT_NS Node &node, const std::size_t relativePosition = 0) const {
    const auto position = static_cast<std::size_t>(node.payload + (relativePosition * sizeof(T)));
    require(position, sizeof(T));
    if constexpr (sizeof(T) == 1) {
      return static_cast<T>(std::to_integer<std::uint8_t>(data_.data()[position]));
    } else {
      typename NBT_NS utils::BitsType<sizeof(T)>::bits bits;
      std::memcpy(&bits, data_.data() + position, sizeof(T));
#ifndef NBT_BIG_ENDIAN
      bits = std::byteswap(bits);
#endif
      return bitCast<T>(bits);
    }
  }

  template <class T>
  [[nodiscard]] T readNumber() {
    require(sizeof(T));
    const auto value = readNumber<T>(position_);
    position_ += sizeof(T);
    return value;
  }

  [[nodiscard]] std::string_view readString(std::size_t offset) const {
    const auto size = static_cast<std::size_t>(readNumber<std::uint16_t>(offset));
    require(offset + 2, size);
    return text(offset + 2, size);
  }

  [[nodiscard]] std::string_view readString(const NBT_NS Node &node, const std::size_t relativePosition = 0) const {
    std::size_t position = node.payload;

    if (node.type == NBT_NS Type::List && relativePosition > 0) {
      for (std::size_t index = 0; index < relativePosition; index++) {
        const auto size = static_cast<std::size_t>(readNumber<std::uint16_t>(position));
        position = skip(position, 2);
        position = skip(position, size);
      }
    }

    const auto size = static_cast<std::size_t>(readNumber<std::uint16_t>(position));
    position = skip(position, 2);
    require(position, size);
    return text(position, size);
  }

  [[nodiscard]] std::string_view readString() {
    const auto str = readString(position_);
    position_ += str.size() + 2;
    return str;
  }

  template <typename T>
    requires(NBT_NS Tag::isNBTNumber<T>)
  [[nodiscard]] NBT_NS Tag::Array<T> readArray(const NBT_NS Node &node) const {
    std::size_t position = node.payload;
    const auto count = static_cast<std::size_t>(node.childCount);
    require(position, count * sizeof(T));
    if constexpr (sizeof(T) == 1) {
      NBT_NS Tag::ByteArray data(count);
      std::memcpy(data.data(), data_.data() + position, count);
      return data;
    } else {
      NBT_NS Tag::Array<T> data(count);
      for (std::size_t index = 0; index < count; index++) {
        data[index] = readNumber<T>(position);
        position = skip(position, sizeof(T));
      }
      return data;
    }
  }

  [[nodiscard]] NBT_NS Tag::Array<NBT_NS Tag::String> readStringList(const NBT_NS Node &node) const {
    std::size_t position = node.payload;
    const auto count = static_cast<std::size_t>(node.childCount);
    NBT_NS Tag::Array<NBT_NS Tag::String> data(count);
    for (std::size_t index = 0; index < count; index++) {
      const auto str = readString(position);
      position = skip(position, 2 + str.size());
      data[index] = str;
    }
    return data;
  }

  [[nodiscard]] NBT_NS Tag::Array<NBT_NS Tag> readSubList(const NBT_NS Node &node) const {
    const auto count = static_cast<std::size_t>(node.childCount);
    NBT_NS Tag::Array<NBT_NS Tag> data(count);
    std::uint32_t childIndex = node.firstChild;
    for (std::size_t index = 0; index < count; index++) {
      const auto &childNode = nodes_[childIndex];
      data[index] = readList(childNode);
      childIndex = childNode.nextSibling;
    }
    return data;
  }

  [[nodiscard]] NBT_NS Tag::Array<NBT_NS Tag> readCompoundList(const NBT_NS Node &node) const {
    const auto count = static_cast<std::size_t>(node.childCount);
    NBT_NS Tag::Array<NBT_NS Tag> data(count);
    std::uint32_t childIndex = node.firstChild;
    for (std::size_t index = 0; index < count; index++) {
      const auto &childNode = nodes_[childIndex];
      data[index] = readCompound(childNode);
      childIndex = childNode.nextSibling;
    }
    return data;
  }

  template <typename T>
  [[nodiscard]] NBT_NS Tag::Array<NBT_NS Tag> readSubArray(const NBT_NS Node &node) const {
    const auto count = static_cast<std::size_t>(node.childCount);
    NBT_NS Tag::Array<NBT_NS Tag> data(count);
    std::uint32_t childIndex = node.firstChild;
    for (std::size_t index = 0; index < count; index++) {
      const auto &childNode = nodes_[childIndex];
      data[index] = readArray<T>(childNode);
      childIndex = childNode.nextSibling;
    }
    return data;
  }

  [[nodiscard]] NBT_NS Tag readList(const NBT_NS Node &node) const {
    const auto type = node.elementType;

    NBT_NS Tag::List list;
    switch (type) {
    case NBT_NS Type::Byte: {
      list = readArray<NBT_NS Tag::Byte>(node);
      break;
    }
    case NBT_NS Type::Short: {
      list = readArray<NBT_NS Tag::Short>(node);
      break;
    }
    case NBT_NS Type::Int: {
      list = readArray<NBT_NS Tag::Int>(node);
      break;
    }
    case NBT_NS Type::Long: {
      list = readArray<NBT_NS Tag::Long>(node);
      break;
    }
    case NBT_NS Type::Float: {
      list = readArray<NBT_NS Tag::Float>(node);
      break;
    }
    case NBT_NS Type::Double: {
      list = readArray<NBT_NS Tag::Double>(node);
      break;
    }
    case NBT_NS Type::String:
      list = readStringList(node);
      break;
    case NBT_NS Type::List:
      list = readSubList(node);
      break;
    case NBT_NS Type::Compound: {
      list = readCompoundList(node);
      break;
    }
    case NBT_NS Type::ByteArray: {
      list = readSubArray<NBT_NS Tag::Byte>(node);
      break;
    }
    case NBT_NS Type::IntArray: {
      list = readSubArray<NBT_NS Tag::Int>(node);
      break;
    }
    case NBT_NS Type::LongArray: {
      list = readSubArray<NBT_NS Tag::Long>(node);
      break;
    }
    case NBT_NS Type::End:
    default:
      break;
    }

    NBT_NS Tag value;
    value.type(NBT_NS Type::List);
    value.elementType(node.elementType);
    value.payload(std::move(list));
    return value;
  }

  [[nodiscard]] NBT_NS Tag readCompound(const NBT_NS Node &node) const {
    NBT_NS Tag::Compound values;
    std::uint32_t childIndex = node.firstChild;
    for (std::size_t index = 0; index < node.childCount; ++index) {
      const auto &childNode = nodes_[childIndex];
      values.insert_or_assign(NBT_NS Tag::String(readName(childNode)), readTag(childIndex));
      childIndex = childNode.nextSibling;
    }

    return values;
  }

  [[nodiscard]] std::byte peek() const {
    require(1);
    return data_.data()[position_];
  }

  [[nodiscard]] std::size_t skip(std::size_t offset, std::size_t count) const {
    require(offset, count);
    return offset + count;
  }

  void skip(std::size_t count) {
    require(count);
    position_ += count;
  }

  void require(std::size_t offset, std::size_t count) const {
    if (count > data_.size() - std::min(data_.size(), offset)) {
      throw NeedMoreDataException("need more data", offset);
    }
  }

  void require(std::size_t count) const {
    if (count > data_.size() - std::min(data_.size(), position_)) {
      throw NeedMoreDataException("need more data", position_);
    }
  }

  [[nodiscard]] std::string_view text(std::size_t offset, std::size_t size) const {
    return {reinterpret_cast<const char *>(data_.data() + offset), size};
  }

  [[nodiscard]] NBT_NS Tag readTag(std::uint32_t nodeIndex) const {
    const auto &node = nodes_[nodeIndex];
    switch (node.type) {
    case NBT_NS Type::Byte:
      return readNumber<NBT_NS Tag::Byte>(node);
    case NBT_NS Type::Short:
      return readNumber<NBT_NS Tag::Short>(node);
    case NBT_NS Type::Int:
      return readNumber<NBT_NS Tag::Int>(node);
    case NBT_NS Type::Long:
      return readNumber<NBT_NS Tag::Long>(node);
    case NBT_NS Type::Float:
      return readNumber<NBT_NS Tag::Float>(node);
    case NBT_NS Type::Double:
      return readNumber<NBT_NS Tag::Double>(node);
    case NBT_NS Type::String:
      return readString(node);
    case NBT_NS Type::ByteArray:
      return readArray<NBT_NS Tag::Byte>(node);
    case NBT_NS Type::IntArray:
      return readArray<NBT_NS Tag::Int>(node);
    case NBT_NS Type::LongArray:
      return readArray<NBT_NS Tag::Long>(node);
    case NBT_NS Type::List:
      return readList(node);
    case NBT_NS Type::Compound:
      return readCompound(node);
    case NBT_NS Type::End:
      return Tag{};
    default:
      throw NBT_NS Exception("Invalid type", 0);
    }
  }

  [[nodiscard]] static NBT_NS NbtParser parseImpl(std::span<const std::byte> data, const NBT_NS Options &options, bool atMost) {
    NBT_NS NbtParser document(data);
    document.maxDepth_ = options.maxDepth;
    document.maxContainerElements_ = options.maxContainerElements;
    document.maxTotalNodes_ = options.maxTotalNodes;
    document.maxInputBytes_ = options.maxInputBytes;
    document.named_ = options.named;

    if (data.empty()) [[unlikely]] {
      if (atMost) {
        document.status_ = NBT_NS Status::NeedMoreData;
        return document;
      }
      throw NBT_NS NeedMoreDataException("empty data", 0);
    }

    document.validate(atMost);
    return document;
  }

  template <typename Container>
  [[nodiscard]] static NBT_NS NbtParser parseImpl(const Container &data, const NBT_NS Options &options, bool atMost) {
    NbtParser document;
    document.maxDepth_ = options.maxDepth;
    document.maxContainerElements_ = options.maxContainerElements;
    document.maxTotalNodes_ = options.maxTotalNodes;
    document.maxInputBytes_ = options.maxInputBytes;
    document.named_ = options.named;

    if (data.empty()) {
      if (atMost) {
        document.status_ = NBT_NS Status::NeedMoreData;
        return document;
      }
      throw NBT_NS NeedMoreDataException("data empty", 0);
    }

    const std::span<const std::byte> view = asBytes(data);
    document.appendImpl(view);
    document.validate(atMost);
    return document;
  }

  template <std::size_t MAX = NBT_NS utils::MAX_COUNT>
  [[nodiscard]] static std::uint32_t checkedOffset(std::size_t value) {
    if (value > MAX) {
      throw Exception(std::string("NBT offset exceeds ") + std::to_string(MAX), value);
    }
    return static_cast<std::uint32_t>(value);
  }

  static std::size_t stringSize(const NBT_NS Tag &tag) {
    const auto *string = std::get_if<NBT_NS Tag::String>(&tag.payload());
    return 2 + (string == nullptr ? 0 : string->size());
  }

  template <typename T>
  static std::size_t arraySize(const NBT_NS Tag &tag) noexcept {
    const auto *values = std::get_if<NBT_NS Tag::Array<T>>(&tag.payload());
    return 4 + (values == nullptr ? 0 : values->size() * sizeof(T));
  }

  static std::size_t listSize(const NBT_NS Tag &tag) noexcept {
    std::size_t size = 5;
    const auto *list = std::get_if<NBT_NS Tag::List>(&tag.payload());
    if (list == nullptr) {
      return size;
    }
    switch (tag.elementType()) {
    case NBT_NS Type::Byte:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::Byte>>(list)) {
        size += values->size();
      }
      break;
    case NBT_NS Type::Short:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::Short>>(list)) {
        size += values->size() * 2;
      }
      break;
    case NBT_NS Type::Int:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::Int>>(list)) {
        size += values->size() * 4;
      }
      break;
    case NBT_NS Type::Long:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::Long>>(list)) {
        size += values->size() * 8;
      }
      break;
    case NBT_NS Type::Float:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::Float>>(list)) {
        size += values->size() * 4;
      }
      break;
    case NBT_NS Type::Double:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::Double>>(list)) {
        size += values->size() * 8;
      }
      break;
    case NBT_NS Type::String:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag::String>>(list)) {
        for (const auto &element : *values) {
          size += 2 + element.size();
        }
      }
      break;
    case NBT_NS Type::ByteArray:
    case NBT_NS Type::IntArray:
    case NBT_NS Type::LongArray:
    case NBT_NS Type::List:
    case NBT_NS Type::Compound:
      if (const auto *values = std::get_if<NBT_NS Tag::Array<NBT_NS Tag>>(list)) {
        for (const auto &element : *values) {
          if (element.type() == tag.elementType()) {
            size += tagSize(tag, element);
          }
        }
      }
      break;
    case NBT_NS Type::End:
    default:
      break;
    }
    return size;
  }

  static std::size_t compoundSize(const NBT_NS Tag &tag) noexcept {
    std::size_t size = 1;
    if (const auto *children = std::get_if<NBT_NS Tag::Compound>(&tag.payload())) {
      for (const auto &[childName, child] : *children) {
        if (child.type() != NBT_NS Type::End) {
          size += tagSize(tag, child, childName);
        }
      }
    }
    return size;
  }

  template <typename T>
  static T scalarOrDefault(const NBT_NS Tag &tag) {
    const auto *value = std::get_if<T>(&tag.payload());
    return value == nullptr ? T{} : *value;
  }

  template <typename T>
  static const NBT_NS Tag::Array<T> &listValues(const NBT_NS Tag &tag) noexcept {
    if (const auto *list = std::get_if<NBT_NS Tag::List>(&tag.payload())) {
      if (const auto *values = std::get_if<NBT_NS Tag::Array<T>>(list)) {
        return *values;
      }
    }
    static const NBT_NS Tag::Array<T> empty;
    return empty;
  }

  template <typename T>
  static const NBT_NS Tag::Array<T> &arrayValues(const NBT_NS Tag &tag) noexcept {
    if (const auto *values = std::get_if<NBT_NS Tag::Array<T>>(&tag.payload())) {
      return *values;
    }
    static const NBT_NS Tag::Array<T> empty;
    return empty;
  }

  static void appendString(BufferWriter &output, std::string_view value) {
    if (value.size() > NBT_NS utils::MAX_STR_SIZE) {
      throw std::length_error("NBT string exceeds 65535 bytes");
    }
    output.writeBE(static_cast<std::uint16_t>(value.size()));
    output.write(value.data(), value.size());
  }

  static void appendLength(BufferWriter &output, std::size_t size) {
    if (size > NBT_NS utils::MAX_COUNT) {
      throw std::length_error("NBT container is too large");
    }
    output.writeBE(static_cast<std::uint32_t>(size));
  }

  [[nodiscard]] static bool checkTag(const NBT_NS Tag &value) noexcept {
    return value.type() != NBT_NS Type::End;
  }

  static void appendTag(BufferWriter &output, std::string_view name, const NBT_NS Tag &value) {
    if (value.type() == NBT_NS Type::End) {
      throw std::invalid_argument("TAG_End");
    }
    output.put(static_cast<std::byte>(value.type()));

    appendString(output, name);

    appendPayload(output, value);
  }

  static void appendListHeader(BufferWriter &output, NBT_NS Type elementType, std::size_t count) {
    output.writeBE(static_cast<std::uint8_t>(count == 0 ? NBT_NS Type::End : elementType));
    appendLength(output, count);
  }

  template <typename T>
  static void appendScalars(BufferWriter &output, const NBT_NS Tag &value) {
    const auto &values = listValues<T>(value);
    appendListHeader(output, value.elementType(), values.size());
    for (const auto &element : values) {
      output.writeBE(element);
    }
  }

  static void appendList(BufferWriter &output, const NBT_NS Tag &value) {
    const NBT_NS Type elementType = value.elementType();
    switch (elementType) {
    case NBT_NS Type::Byte:
      appendScalars<NBT_NS Tag::Byte>(output, value);
      return;
    case NBT_NS Type::Short:
      appendScalars<NBT_NS Tag::Short>(output, value);
      return;
    case NBT_NS Type::Int:
      appendScalars<NBT_NS Tag::Int>(output, value);
      return;
    case NBT_NS Type::Long:
      appendScalars<NBT_NS Tag::Long>(output, value);
      return;
    case NBT_NS Type::Float:
      appendScalars<NBT_NS Tag::Float>(output, value);
      return;
    case NBT_NS Type::Double:
      appendScalars<NBT_NS Tag::Double>(output, value);
      return;
    case NBT_NS Type::String: {
      const auto &values = listValues<NBT_NS Tag::String>(value);
      appendListHeader(output, NBT_NS Type::String, values.size());
      for (const auto &element : values) {
        appendString(output, element);
      }
      return;
    }
    case NBT_NS Type::ByteArray:
    case NBT_NS Type::IntArray:
    case NBT_NS Type::LongArray:
    case NBT_NS Type::List:
    case NBT_NS Type::Compound: {
      const auto &values = listValues<NBT_NS Tag>(value);
      const auto count = static_cast<std::size_t>(std::count_if(values.begin(), values.end(), [elementType](const NBT_NS Tag &element) {
        return element.type() == elementType;
      }));
      appendListHeader(output, elementType, count);
      for (const auto &element : values) {
        if (element.type() == elementType) {
          appendPayload(output, element);
        }
      }
      return;
    }
    case NBT_NS Type::End:
    default:
      appendListHeader(output, NBT_NS Type::End, 0);
      return;
    }
  }

  static void appendPayload(BufferWriter &output, const NBT_NS Tag &value) {
    switch (value.type()) {
    case NBT_NS Type::Byte:
      output.put(static_cast<std::byte>(scalarOrDefault<NBT_NS Tag::Byte>(value)));
      break;
    case NBT_NS Type::Short:
      output.writeBE(scalarOrDefault<NBT_NS Tag::Short>(value));
      break;
    case NBT_NS Type::Int:
      output.writeBE(scalarOrDefault<NBT_NS Tag::Int>(value));
      break;
    case NBT_NS Type::Long:
      output.writeBE(scalarOrDefault<NBT_NS Tag::Long>(value));
      break;
    case NBT_NS Type::Float:
      output.writeBE(scalarOrDefault<NBT_NS Tag::Float>(value));
      break;
    case NBT_NS Type::Double:
      output.writeBE(scalarOrDefault<NBT_NS Tag::Double>(value));
      break;
    case NBT_NS Type::String:
      appendString(output, scalarOrDefault<NBT_NS Tag::String>(value));
      break;
    case NBT_NS Type::ByteArray: {
      const auto &values = arrayValues<NBT_NS Tag::Byte>(value);
      appendLength(output, values.size());
      output.write(values.data(), values.size());
      break;
    }
    case NBT_NS Type::IntArray: {
      const auto &values = arrayValues<NBT_NS Tag::Int>(value);
      appendLength(output, values.size());
      for (const auto &element : values) {
        output.writeBE(element);
      }
      break;
    }
    case NBT_NS Type::LongArray: {
      const auto &values = arrayValues<NBT_NS Tag::Long>(value);
      appendLength(output, values.size());
      for (const auto &element : values) {
        output.writeBE(element);
      }
      break;
    }
    case NBT_NS Type::List:
      appendList(output, value);
      break;
    case NBT_NS Type::Compound:
      if (const auto *children = std::get_if<NBT_NS Tag::Compound>(&value.payload())) {
        for (const auto &[childName, child] : *children) {
          if (checkTag(child)) {
            appendTag(output, childName, child);
          }
        }
      }
      output.put(std::byte{0});
      break;
    case NBT_NS Type::End:
      break;
    }
  }

  template <typename Container>
  [[nodiscard]] static std::span<const std::byte> asBytes(const Container &data) {
    if constexpr (requires { std::as_bytes(std::span(data)); }) {
      return std::as_bytes(std::span(data));
    } else {
      return std::span<const std::byte>{data.data(), data.size()};
    }
  }

  template <typename T, typename B = T, typename V>
  [[nodiscard]] static B bitCast(const V &value) {
    if constexpr (std::is_floating_point_v<T>) {
      return std::bit_cast<B>(value);
    } else {
      return static_cast<B>(value);
    }
  }

  NBT_NS Buffer data_;
  NBT_NS Status status_{NBT_NS Status::Empty};

  std::optional<NBT_NS Tag> rootValue_;
  std::vector<NBT_NS Node> nodes_;

  std::size_t position_{0};
  std::size_t next_node_{0};

  std::size_t maxDepth_{512};
  std::size_t maxContainerElements_{static_cast<std::size_t>(16U * 1024U * 1024U)};
  std::size_t maxTotalNodes_{static_cast<std::size_t>(64U * 1024U * 1024U)};
  std::size_t maxInputBytes_{static_cast<std::size_t>(1024U * 1024U * 1024U)};
  bool named_{false};

  // Lifetime
  std::shared_ptr<Lifetime> lifetime_ = std::make_shared<Lifetime>(this);
};

namespace utils {
template <>
struct BitsType<2> {
  using bits = std::uint16_t;
};

template <>
struct BitsType<4> {
  using bits = std::uint32_t;
};

template <>
struct BitsType<8> {
  using bits = std::uint64_t;
};

template <>
struct TypeTraits<NBT_NS Type::Byte> {
  using value_t = NBT_NS Tag::Byte;
  using element_t = NBT_NS Tag::Byte;
  static constexpr bool isScalar = true;
  static constexpr bool isArray = false;
  static constexpr std::size_t fixedSize = 1;
};

template <>
struct TypeTraits<NBT_NS Type::Short> {
  using value_t = NBT_NS Tag::Short;
  using element_t = NBT_NS Tag::Short;
  static constexpr bool isScalar = true;
  static constexpr bool isArray = false;
  static constexpr std::size_t fixedSize = 2;
};

template <>
struct TypeTraits<NBT_NS Type::Int> {
  using value_t = NBT_NS Tag::Int;
  using element_t = NBT_NS Tag::Int;
  static constexpr bool isScalar = true;
  static constexpr bool isArray = false;
  static constexpr std::size_t fixedSize = 4;
};

template <>
struct TypeTraits<NBT_NS Type::Long> {
  using value_t = NBT_NS Tag::Long;
  using element_t = NBT_NS Tag::Long;
  static constexpr bool isScalar = true;
  static constexpr bool isArray = false;
  static constexpr std::size_t fixedSize = 8;
};

template <>
struct TypeTraits<NBT_NS Type::Float> {
  using value_t = NBT_NS Tag::Float;
  using element_t = NBT_NS Tag::Float;
  static constexpr bool isScalar = true;
  static constexpr bool isArray = false;
  static constexpr std::size_t fixedSize = 4;
};

template <>
struct TypeTraits<NBT_NS Type::Double> {
  using value_t = NBT_NS Tag::Double;
  using element_t = NBT_NS Tag::Double;
  static constexpr bool isScalar = true;
  static constexpr bool isArray = false;
  static constexpr std::size_t fixedSize = 8;
};

template <>
struct TypeTraits<NBT_NS Type::String> {
  using value_t = NBT_NS Tag::String;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = false;
};

template <>
struct TypeTraits<NBT_NS Type::ByteArray> {
  using value_t = NBT_NS Tag::ByteArray;
  using element_t = NBT_NS Tag::Byte;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = true;
};

template <>
struct TypeTraits<NBT_NS Type::IntArray> {
  using value_t = NBT_NS Tag::IntArray;
  using element_t = NBT_NS Tag::Int;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = true;
};

template <>
struct TypeTraits<NBT_NS Type::LongArray> {
  using value_t = NBT_NS Tag::LongArray;
  using element_t = NBT_NS Tag::Long;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = true;
};

template <>
struct TypeTraits<NBT_NS Type::List> {
  using value_t = NBT_NS Tag::List;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = false;
};

template <>
struct TypeTraits<NBT_NS Type::Compound> {
  using value_t = NBT_NS Tag::Compound;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = false;
};

template <>
struct TypeTraits<NBT_NS Type::End> {
  using value_t = NBT_NS Tag::End;
  static constexpr bool isScalar = false;
  static constexpr bool isArray = false;
};

template <typename T>
struct TypeOf {
  static constexpr Type value = NBT_NS Type::End;
};

template <>
struct TypeOf<NBT_NS Tag::End> {
  static constexpr Type value = NBT_NS Type::End;
};

template <>
struct TypeOf<NBT_NS Tag::Byte> {
  static constexpr Type value = NBT_NS Type::Byte;
};

template <>
struct TypeOf<NBT_NS Tag::Short> {
  static constexpr Type value = NBT_NS Type::Short;
};

template <>
struct TypeOf<NBT_NS Tag::Int> {
  static constexpr Type value = NBT_NS Type::Int;
};

template <>
struct TypeOf<NBT_NS Tag::Long> {
  static constexpr Type value = NBT_NS Type::Long;
};

template <>
struct TypeOf<NBT_NS Tag::Float> {
  static constexpr Type value = NBT_NS Type::Float;
};

template <>
struct TypeOf<NBT_NS Tag::Double> {
  static constexpr Type value = NBT_NS Type::Double;
};

template <>
struct TypeOf<NBT_NS Tag::String> {
  static constexpr Type value = NBT_NS Type::String;
};

template <>
struct TypeOf<NBT_NS Tag::ByteArray> {
  static constexpr Type value = NBT_NS Type::ByteArray;
};

template <>
struct TypeOf<NBT_NS Tag::IntArray> {
  static constexpr Type value = NBT_NS Type::IntArray;
};

template <>
struct TypeOf<NBT_NS Tag::LongArray> {
  static constexpr Type value = NBT_NS Type::LongArray;
};

template <>
struct TypeOf<NBT_NS Tag::List> {
  static constexpr Type value = NBT_NS Type::List;
};

template <>
struct TypeOf<NBT_NS Tag::Compound> {
  static constexpr Type value = NBT_NS Type::Compound;
};

template <>
struct UnderlyingType<NBT_NS Type::Byte, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Byte;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::Short, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Short;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::Int, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Int;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::Long, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Long;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::Float, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Float;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::Double, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Double;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::String, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::String;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Byte> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Byte;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Short> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Short;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Int> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Int;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Long> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Long;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Float> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Float;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Double> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Double;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::List> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::List;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::Compound> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::Compound;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::ByteArray> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::ByteArray;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::IntArray> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::IntArray;
};

template <>
struct UnderlyingType<NBT_NS Type::List, NBT_NS Type::LongArray> {
  using value_t = NBT_NS Tag::List;
  using element_t = NBT_NS Tag::LongArray;
};

template <>
struct UnderlyingType<NBT_NS Type::Compound, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::Compound;
  using element_t = NBT_NS Tag::End;
};

template <>
struct UnderlyingType<NBT_NS Type::ByteArray, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::ByteArray;
  using element_t = NBT_NS Tag::Byte;
};

template <>
struct UnderlyingType<NBT_NS Type::IntArray, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::IntArray;
  using element_t = NBT_NS Tag::Int;
};

template <>
struct UnderlyingType<NBT_NS Type::LongArray, NBT_NS Type::End> {
  using value_t = NBT_NS Tag::LongArray;
  using element_t = NBT_NS Tag::Long;
};
} // namespace utils

} // namespace nbt
