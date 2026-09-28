/**
 * @file nbt.h
 * @author Carlos Machaca (carloscordova96@hotmail.com)
 * @brief Header-only Java Edition NBT codec: structural validation, lazy non-owning views
 *        and big-endian encoding.
 *
 * Error policy:
 * - Malformed input is reported through nbt::Exception with the offending byte offset.
 * - Truncated input never fails: it is reported as nbt::Status::NeedMoreData so the
 *   document can be completed later with NbtParser::append.
 * - During encoding, values that do not match the declared type are ignored instead of
 *   raising errors. List elements whose Tag::type differs from the list Tag::elementType,
 *   and TAG_End children inside compounds, are skipped. Callers are responsible for
 *   providing well-formed input.
 *
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
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "nbt/buffer.h"

namespace nbt {

using namespace std::string_literals;

namespace utils {

constexpr std::size_t MAX_SIZE = static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)());
constexpr std::size_t MAX_STR_SIZE = static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)());
constexpr std::uint32_t NO_NODE = (std::numeric_limits<std::uint32_t>::max)();

template <typename T>
using nbt_number = std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>;
} // namespace utils

/**
 * @brief Common base for NBT exceptions.
 *
 * Carries the byte offset where the problem was detected and an optional nested
 * exception for errors that wrap lower-level failures.
 */
class BaseException {
public:
  BaseException(std::size_t offset) : offset_(offset) {
  }

  BaseException(std::size_t offset, const std::exception_ptr &nested) : offset_(offset), nested_(nested) {
  }

  /**
   * @brief Byte offset in the input where the error was detected.
   */
  [[nodiscard]] std::size_t offset() const noexcept {
    return offset_;
  }

  /**
   * @brief Nested exception that caused this error, if any.
   */
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
class Exception : public BaseException, public std::runtime_error {
public:
  Exception(const std::string &message, std::size_t offset) : BaseException(offset), std::runtime_error(message) {
  }

  Exception(const std::string &message, std::size_t offset, const std::exception_ptr &nested) : BaseException(offset, nested), std::runtime_error(message) {
  }
};

/**
 * @brief Non-fatal truncation signal. Thrown internally when the input ends before a
 *        complete document can be validated; surfaced to callers as Status::NeedMoreData.
 */
class NeedMoreDataException : public BaseException, public std::out_of_range {
public:
  NeedMoreDataException(const std::string &message, std::size_t offset) : BaseException(offset), std::out_of_range(message) {
  }

  NeedMoreDataException(const std::string &message, std::size_t offset, const std::exception_ptr &nested) : BaseException(offset, nested), std::out_of_range(message) {
  }
};

/**
 * @brief NBT tag type identifiers, matching the Java Edition wire ids.
 */
enum class Type : std::uint8_t { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

/**
 * @brief Document state: Empty (no input), Complete (validated) or NeedMoreData
 *        (truncated, more bytes required).
 */
enum class Status : std::uint8_t { Empty, Complete, NeedMoreData };

/**
 * @brief Resource limits enforced while validating input.
 */
struct Options {
  std::size_t maxDepth{512};                                                       ///< Maximum nesting depth.
  std::size_t maxContainerElements{static_cast<std::size_t>(16U * 1024U * 1024U)}; ///< Maximum children per list, compound or array.
  std::size_t maxTotalNodes{static_cast<std::size_t>(64U * 1024U * 1024U)};        ///< Maximum total indexed nodes.
  std::size_t maxInputBytes{static_cast<std::size_t>(1024U * 1024U * 1024U)};      ///< Maximum accepted input size in bytes.
  bool named{true};                                                                ///< Whether the input root tag is expected to carry a name.
};

/**
 * @brief Owning representation of a single NBT tag.
 *
 * A Tag stores a name, an NBT type, an optional list element type and a payload
 * that matches the selected type. Standard C++ scalar values and containers can
 * be converted directly to a Tag through the value constructors.
 *
 * @par Construction rules
 * - Standard C++ scalar and string-like values, as well as @c std::vector
 *   containers, are converted to the corresponding NBT type.
 * - Tag names are always accepted and never validated; they cannot cause the
 *   constructor to fail.
 * - When a Tag is inserted as a child of a TAG_List or of a typed array
 *   (TAG_Byte_Array, TAG_Int_Array, TAG_Long_Array), its name is ignored during
 *   encoding and parsing.
 * - A Tag whose @ref type is Type::End represents an invalid/empty tag and
 *   cannot be encoded as a root tag.
 * - A @c std::vector<Tag> constructed without an explicit element type is
 *   treated as a TAG_Compound.
 * - Passing Type::End as the explicit element type of a list ignores the supplied
 *   children and constructs an empty list.
 * - Encoding is lenient: list elements whose type differs from the list element
 *   type, and TAG_End children inside compounds, are ignored rather than rejected.
 */
struct Tag {

  using Byte = std::int8_t;            ///< Signed 8-bit TAG_Byte payload.
  using Short = std::int16_t;          ///< Signed 16-bit TAG_Short payload.
  using Int = std::int32_t;            ///< Signed 32-bit TAG_Int payload.
  using Long = std::int64_t;           ///< Signed 64-bit TAG_Long payload.
  using Float = float;                 ///< IEEE-754 TAG_Float payload.
  using Double = double;               ///< IEEE-754 TAG_Double payload.
  using String = std::string;          ///< Owning UTF-8 TAG_String payload.
  using ByteArray = std::vector<Byte>; ///< Owning TAG_Byte_Array payload.
  using IntArray = std::vector<Int>;   ///< Owning TAG_Int_Array payload.
  using LongArray = std::vector<Long>; ///< Owning TAG_Long_Array payload.
  using Container = std::vector<Tag>;  ///< Owning TAG_List or TAG_Compound payload.

  using Payload = std::variant<std::monostate, Byte, Short, Int, Long, Float, Double, String, Container, ByteArray, IntArray, LongArray>;

  std::string name;            ///< Tag name. Ignored for list and array elements.
  Type type{Type::End};        ///< NBT type of this tag.
  Type elementType{Type::End}; ///< Element type; only meaningful when @ref type is Type::List.
  Payload payload;             ///< Owned payload matching @ref type.

  /**
   * @brief Construct an unnamed TAG_End tag.
   */
  Tag() = default;

  /**
   * @brief Copy an existing tag and assign a new name.
   * @param name The tag name.
   * @param value The tag to copy.
   */
  Tag(std::string_view name, const Tag &value) : name(name) {
    this->payload = value.payload;
    this->type = value.type;
    this->elementType = value.elementType;
  }

  /**
   * @brief Move an existing tag and assign a new name.
   * @param name The tag name.
   * @param value The tag to move from.
   */
  Tag(std::string_view name, Tag &&value) : name(name), type(value.type), elementType(value.elementType), payload(std::move(value.payload)) {
  }

  /**
   * @brief Construct an unnamed numeric scalar tag.
   * @tparam T One of the supported numeric scalar types (Byte, Short, Int, Long, Float, Double).
   * @param value The scalar value to store.
   */
  template <typename T>
    requires std::disjunction_v<std::is_same<T, Byte>, std::is_same<T, Short>, std::is_same<T, Int>, std::is_same<T, Long>, std::is_same<T, Float>, std::is_same<T, Double>>
  Tag(T value) : Tag("", std::move(value)) {
  }

  /**
   * @brief Construct a named numeric scalar tag.
   * @tparam T One of the supported numeric scalar types (Byte, Short, Int, Long, Float, Double).
   * @param name The tag name.
   * @param value The scalar value to store.
   */
  template <typename T>
    requires std::disjunction_v<std::is_same<T, Byte>, std::is_same<T, Short>, std::is_same<T, Int>, std::is_same<T, Long>, std::is_same<T, Float>, std::is_same<T, Double>>
  Tag(std::string_view name, T value) : name(name) {
    if constexpr (std::is_same_v<T, Byte>) {
      this->payload = std::move(value);
      this->type = Type::Byte;
    } else if constexpr (std::is_same_v<T, Short>) {
      this->payload = std::move(value);
      this->type = Type::Short;
    } else if constexpr (std::is_same_v<T, Int>) {
      this->payload = std::move(value);
      this->type = Type::Int;
    } else if constexpr (std::is_same_v<T, Long>) {
      this->payload = std::move(value);
      this->type = Type::Long;
    } else if constexpr (std::is_same_v<T, Float>) {
      this->payload = std::move(value);
      this->type = Type::Float;
    } else if constexpr (std::is_same_v<T, Double>) {
      this->payload = std::move(value);
      this->type = Type::Double;
    }
  }

  /**
   * @brief Construct an unnamed TAG_String tag from a string-like value.
   * @tparam S A type convertible to std::string_view.
   * @param value The string payload.
   */
  template <typename S>
    requires std::is_constructible_v<std::string_view, S &&>
  Tag(const S &value) : Tag(std::string_view(value)) {
  }

  /**
   * @brief Construct a named TAG_String tag from a string-like value.
   * @tparam S A type convertible to std::string_view.
   * @param name The tag name.
   * @param value The string payload.
   */
  template <typename S>
    requires std::is_constructible_v<std::string_view, S &&>
  Tag(std::string_view name, const S &value) : Tag(name, std::string_view(value)) {
  }

  /**
   * @brief Construct an unnamed TAG_String tag from a string view.
   * @param value The string payload.
   */
  Tag(std::string_view value) : Tag("", value) {
  }

  /**
   * @brief Construct a named TAG_String tag from a string view.
   * @param name The tag name.
   * @param value The string payload.
   */
  Tag(std::string_view name, std::string_view value) : name(name), type(nbt::Type::String), payload(std::string(value)) {
  }

  /**
   * @brief Whether @p T is a valid element type for vector-based constructors.
   */
  template <typename T>
  static constexpr bool isElementType = std::disjunction_v<std::is_same<T, Byte>, std::is_same<T, Short>, std::is_same<T, Int>, std::is_same<T, Long>, std::is_same<T, Float>, std::is_same<T, Double>, std::is_same<T, String>, std::is_same<T, Tag>>;

  /**
   * @brief Construct an unnamed typed-array, list or compound tag from an initializer list.
   * @tparam T The element type. See the std::vector overload for the type mapping.
   * @param elements The elements to store.
   */
  template <typename T>
    requires isElementType<T>
  Tag(std::initializer_list<T> elements) : Tag("", elements) {
  }

  /**
   * @brief Construct an unnamed typed-array, list or compound tag from a vector.
   * @tparam T The element type. Byte, Int and Long produce typed arrays; Short, Float, Double and
   *           String produce lists; Tag produces a compound.
   * @param elements The elements to store.
   */
  template <typename T>
    requires isElementType<T>
  Tag(std::vector<T> elements) : Tag("", std::move(elements)) {
  }

  /**
   * @brief Construct a named typed-array, list or compound tag from a range.
   * @tparam R A range whose element type is one of the supported element types. Byte, Int and Long
   *           produce typed arrays; Short, Float, Double and String produce lists; Tag produces a compound.
   * @param name The tag name.
   * @param elements The elements to store.
   */
  template <typename R>
    requires isElementType<typename R::value_type>
  Tag(std::string_view name, R elements) : name(name) {
    using T = typename R::value_type;
    if constexpr (std::is_same_v<T, Byte> || std::is_same_v<T, Int> || std::is_same_v<T, Long> || std::is_same_v<T, Tag>) {
      this->payload = std::move(elements);
    } else {
      Tag::Container container;
      std::ranges::transform(elements, std::back_inserter(container), [](auto &value) {
        return Tag(value);
      });
      this->payload = std::move(container);
    }
    if constexpr (std::is_same_v<T, Byte>) {
      this->type = Type::ByteArray;
    } else if constexpr (std::is_same_v<T, Short>) {
      this->type = Type::List;
      this->elementType = Type::Short;
    } else if constexpr (std::is_same_v<T, Int>) {
      this->type = Type::IntArray;
    } else if constexpr (std::is_same_v<T, Long>) {
      this->type = Type::LongArray;
    } else if constexpr (std::is_same_v<T, Float>) {
      this->type = Type::List;
      this->elementType = Type::Float;
    } else if constexpr (std::is_same_v<T, Double>) {
      this->type = Type::List;
      this->elementType = Type::Double;
    } else if constexpr (std::is_same_v<T, String>) {
      this->type = Type::List;
      this->elementType = Type::String;
    } else if constexpr (std::is_same_v<T, Tag>) {
      this->type = Type::Compound;
    }
  }

  /**
   * @brief Construct an unnamed TAG_List with an explicit element type.
   * @param type The element type of the list. Type::End constructs an empty list.
   * @param value The list elements. Elements are ignored for Type::End; otherwise, the supplied
   *              values are stored as-is and the caller is responsible for their correctness.
   */
  Tag(Type type, std::vector<Tag> value = {}) : Tag("", type, std::move(value)) {
  }

  /**
   * @brief Construct a named TAG_List with an explicit element type.
   * @param name The tag name.
   * @param type The element type of the list. Type::End constructs an empty list.
   * @param value The list elements. Elements are ignored for Type::End; otherwise, the supplied
   *              values are stored as-is and the caller is responsible for their correctness.
   */
  Tag(std::string_view name, Type type, std::vector<Tag> value = {}) : name(name), type(Type::List), elementType(type) {
    if (type == Type::End) {
      this->payload = Container{};
      return;
    }

    this->payload = std::move(value);
  }

  /**
   * @brief Check whether the tag is not TAG_End.
   * @return true if the tag has a concrete NBT type, false if it represents TAG_End.
   */
  operator bool() const {
    return type != Type::End;
  }
};

class NbtUtilities;

/**
 * @brief NBT document: validates input lazily, exposes non-owning views and encodes tags.
 *
 * Parsing indexes the complete structure without decoding values into an owning tree.
 * Truncated input is not an error: the document reports Status::NeedMoreData and can be
 * completed later with @ref append. Malformed input throws nbt::Exception.
 *
 * @par Encoding policy
 * Encoding never rejects mismatched values. List elements whose Tag::type differs from
 * the list Tag::elementType are ignored, and TAG_End children inside compounds are
 * skipped. The encoded list length counts only the elements actually written. Callers
 * are responsible for providing well-formed input.
 *
 * @tparam BufferT Owning contiguous byte storage used for accumulated input and encoded
 *                 output. Defaults to nbt::Buffer.
 */
template <typename BufferT = nbt::Buffer>
class NbtParser final {
public:
  /**
   * @brief Validate borrowed bytes, tolerating truncation.
   * @param data Input bytes. The document borrows the buffer and must not outlive it.
   * @return A document in Status::Complete, Status::NeedMoreData or Status::Empty.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  [[nodiscard]] static NbtParser parseAtMost(std::span<const std::byte> data) {
    Options options;
    return parseAtMost(data, options);
  }

  /**
   * @brief Validate borrowed bytes with limits, tolerating truncation.
   * @param data Input bytes. The document borrows the buffer and must not outlive it.
   * @param options Resource limits applied during validation.
   * @return A document in Status::Complete, Status::NeedMoreData or Status::Empty.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  [[nodiscard]] static NbtParser parseAtMost(std::span<const std::byte> data, const Options &options) {
    return parseImpl(data, options, true);
  }

  /**
   * @brief Validate borrowed bytes; truncated input throws.
   * @param data Input bytes. The document borrows the buffer and must not outlive it.
   * @return A document in Status::Complete.
   * @throws nbt::NeedMoreDataException on empty or truncated input.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  [[nodiscard]] static NbtParser parse(std::span<const std::byte> data) {
    Options options;
    return parse(data, options);
  }

  /**
   * @brief Validate borrowed bytes with limits; truncated input throws.
   * @param data Input bytes. The document borrows the buffer and must not outlive it.
   * @param options Resource limits applied during validation.
   * @return A document in Status::Complete.
   * @throws nbt::NeedMoreDataException on empty or truncated input.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  [[nodiscard]] static NbtParser parse(std::span<const std::byte> data, const Options &options) {
    return parseImpl(data, options, false);
  }

  /**
   * @brief Copy a contiguous container into the internal buffer and validate it,
   *        tolerating truncation.
   * @tparam Container A contiguous byte container.
   * @param data Input bytes to copy and own.
   * @return A document in Status::Complete, Status::NeedMoreData or Status::Empty.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  template <typename Container>
  [[nodiscard]] static NbtParser parseAtMost(const Container &data) {
    Options options;
    return parseAtMost(data, options);
  }

  /**
   * @brief Copy a contiguous container into the internal buffer and validate it with
   *        limits, tolerating truncation.
   * @tparam Container A contiguous byte container.
   * @param data Input bytes to copy and own.
   * @param options Resource limits applied during validation.
   * @return A document in Status::Complete, Status::NeedMoreData or Status::Empty.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  template <typename Container>
  [[nodiscard]] static NbtParser parseAtMost(const Container &data, const Options &options) {
    return parseImpl(data, options, true);
  }

  /**
   * @brief Copy a contiguous container into the internal buffer and validate it;
   *        truncated input throws.
   * @tparam Container A contiguous byte container.
   * @param data Input bytes to copy and own.
   * @return A document in Status::Complete.
   * @throws nbt::NeedMoreDataException on empty or truncated input.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  template <typename Container>
  [[nodiscard]] static NbtParser parse(const Container &data) {
    Options options;
    return parse(data, options);
  }

  /**
   * @brief Copy a contiguous container into the internal buffer and validate it with
   *        limits; truncated input throws.
   * @tparam Container A contiguous byte container.
   * @param data Input bytes to copy and own.
   * @param options Resource limits applied during validation.
   * @return A document in Status::Complete.
   * @throws nbt::NeedMoreDataException on empty or truncated input.
   * @throws nbt::Exception on malformed input or violated limits.
   */
  template <typename Container>
  [[nodiscard]] static NbtParser parse(const Container &data, const Options &options) {
    return parseImpl(data, options, false);
  }

private:
  [[nodiscard]] static NbtParser parseImpl(std::span<const std::byte> data, const Options &options, bool atMost) {
    nbt::NbtParser<BufferT> document;
    document.setMaxDepth(options.maxDepth);
    document.setMaxContainerElements(options.maxContainerElements);
    document.setMaxTotalNodes(options.maxTotalNodes);
    document.setMaxInputBytes(options.maxInputBytes);

    if (data.empty()) [[unlikely]] {
      if (atMost) {
        document.status_ = Status::NeedMoreData;
        return document;
      }
      throw nbt::NeedMoreDataException("empty data", 0);
    }

    document.data_ = data;
    document.validate(atMost, options.named);
    return document;
  }

  template <typename Container>
  [[nodiscard]] static NbtParser parseImpl(const Container &data, const Options &options, bool atMost) {
    NbtParser document;
    document.setMaxDepth(options.maxDepth);
    document.setMaxContainerElements(options.maxContainerElements);
    document.setMaxTotalNodes(options.maxTotalNodes);
    document.setMaxInputBytes(options.maxInputBytes);

    if (data.empty()) {
      if (atMost) {
        document.status_ = Status::NeedMoreData;
        return document;
      }
      throw nbt::NeedMoreDataException("data empty", 0);
    }

    const std::span<const std::byte> view = asBytes(data);
    document.appendImpl(view);
    document.validate(atMost, options.named);
    return document;
  }

  /**
   * @brief Structural index entry: byte ranges and sibling/child links into the input.
   */
  struct Node {
    std::uint32_t begin{};       ///< Offset where the tag payload begins.
    std::uint32_t end{};         ///< Offset where the tag ends.
    std::uint32_t payload{};     ///< Offset of the decodable payload bytes.
    std::uint32_t nameOffset{};  ///< Offset of the tag name bytes.
    std::uint32_t firstChild{};  ///< Index of the first child node, or @ref noNode.
    std::uint32_t nextSibling{}; ///< Index of the next sibling node, or @ref noNode.
    std::uint32_t childCount{};  ///< Number of direct children.
    std::uint16_t nameSize{};    ///< Tag name length in bytes.
    Type type{Type::End};        ///< NBT type of the node.
    Type elementType{Type::End}; ///< List element type, when @ref type is Type::List.
  };

  /**
   * @brief Bounds-checked byte writer over a preallocated buffer region.
   */
  class BufferWriter final {
  public:
    BufferWriter(std::byte *data, std::size_t capacity) : begin_(data), current_(data), end_(capacity == 0 ? data : data + capacity) {
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
        nbt::utils::nbt_number<T> bits = bitCast<T, nbt::utils::nbt_number<T>>(value);
#ifndef NBT_BIG_ENDIAN
        bits = std::byteswap(bits);
#endif
        write(&bits, sizeof(T));
      }
    }

    template <typename T>
    void writeLE(T value) {
      if constexpr (sizeof(T) == 1) {
        put(static_cast<std::byte>(static_cast<std::uint8_t>(value)));
      } else {
        nbt::utils::nbt_number<T> bits = bitCast<T, nbt::utils::nbt_number<T>>(value);
#ifdef NBT_BIG_ENDIAN
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

  friend class nbt::NbtUtilities;

public:
  /**
   * @brief Lazy big-endian view over a TAG_Int_Array or TAG_Long_Array payload.
   *
   * Elements are decoded on access directly from the document bytes; the view does not
   * own or extend the lifetime of the underlying data.
   *
   * @tparam T Decoded element type (Tag::Int or Tag::Long).
   */
  template <typename T>
  class ArrayView {
  public:
    /**
     * @brief Forward iterator that decodes elements on dereference.
     */
    class Iterator {
    public:
      Iterator() = default;

      Iterator(const ArrayView *view, std::size_t index) : view_(view), index_(index) {
      }

      [[nodiscard]] T operator*() const {
        return (*view_)[index_];
      }

      Iterator &operator++() noexcept {
        ++index_;
        return *this;
      }

      Iterator operator++(int) noexcept {
        Iterator copy{view_, index_};
        ++index_;
        return copy;
      }

      [[nodiscard]] bool operator==(const Iterator &other) const noexcept {
        return view_ == other.view_ && index_ == other.index_;
      }

      [[nodiscard]] bool operator!=(const Iterator &other) const noexcept {
        return !(*this == other);
      }

    private:
      const ArrayView *view_{};
      std::size_t index_{0};
    };

    ArrayView() = default;

    ArrayView(const NbtParser *owner, std::size_t payload) : owner_(owner), payload_(payload) {
    }

    /**
     * @brief Number of elements in the array.
     */
    [[nodiscard]] std::size_t size() const noexcept {
      return owner_ ? static_cast<std::size_t>(owner_->readNumber<std::int32_t>(payload_)) : 0;
    }

    /**
     * @brief Whether the array has no elements.
     */
    [[nodiscard]] bool empty() const noexcept {
      return size() == 0;
    }

    /**
     * @brief Decode the element at @p index.
     * @throws nbt::NeedMoreDataException if @p index is out of bounds.
     */
    [[nodiscard]] T operator[](std::size_t index) const {
      if (index >= size()) {
        throw nbt::NeedMoreDataException("overflow", index);
      }
      return owner_->readNumber<T>(payload_ + 4 + (sizeof(T) * index));
    }

    /**
     * @brief Decode the first element.
     */
    [[nodiscard]] T front() const {
      return (*this)[0];
    }

    /**
     * @brief Decode the last element.
     */
    [[nodiscard]] T back() const {
      return (*this)[size() - 1];
    }

    [[nodiscard]] Iterator begin() const noexcept {
      return {this, 0};
    }

    [[nodiscard]] Iterator end() const noexcept {
      return {this, size()};
    }

  private:
    const NbtParser *owner_{};
    std::size_t payload_{0};
  };

  using IntArrayView = ArrayView<Tag::Int>;   ///< Lazy view over TAG_Int_Array elements.
  using LongArrayView = ArrayView<Tag::Long>; ///< Lazy view over TAG_Long_Array elements.

  /**
   * @brief Non-owning lazy view of a single node inside a document.
   *
   * Views reference the document that produced them and must not outlive it. Container
   * traversal follows sibling links, so indexed access via @ref child / @ref operator[]
   * is O(index); prefer range iteration in hot loops.
   */
  class View {
  public:
    /**
     * @brief Forward iterator over the children of a container view.
     */
    class Iterator {
    public:
      Iterator() = default;

      Iterator(const NbtParser *owner, std::uint32_t node) : owner_(owner), node_(node) {
      }

      [[nodiscard]] View operator*() const {
        return {owner_, node_};
      }

      Iterator &operator++() {
        node_ = owner_->nodes_[node_].nextSibling;
        return *this;
      }

      Iterator operator++(int) {
        Iterator copy{*this};
        ++*this;
        return copy;
      }

      [[nodiscard]] bool operator==(const Iterator &other) const noexcept {
        return owner_ == other.owner_ && node_ == other.node_;
      }

      [[nodiscard]] bool operator!=(const Iterator &other) const noexcept {
        return !(*this == other);
      }

    private:
      const NbtParser *owner_{};
      std::uint32_t node_{0};
    };

    View() = default;

    /**
     * @brief Whether the view references a valid node.
     */
    [[nodiscard]] explicit operator bool() const noexcept {
      return owner_ != nullptr;
    }

    /**
     * @brief NBT type of the referenced tag.
     */
    [[nodiscard]] Type type() const {
      return node().type;
    }

    /**
     * @brief Element type when the tag is a TAG_List, Type::End otherwise.
     */
    [[nodiscard]] Type elementType() const {
      return owner_ != nullptr ? node().elementType : Type::End;
    }

    /**
     * @brief Byte offset where the tag payload begins in the document buffer.
     */
    [[nodiscard]] std::size_t payloadBegin() const {
      return node().begin;
    }

    /**
     * @brief Byte offset where the tag ends in the document buffer.
     */
    [[nodiscard]] std::size_t payloadEnd() const {
      return node().end;
    }

    /**
     * @brief Tag name, empty for unnamed tags such as list elements.
     */
    [[nodiscard]] std::string_view name() const {
      const auto &entry = node();
      return owner_->text(entry.nameOffset, entry.nameSize);
    }

    /**
     * @brief Number of direct children (list/compound elements, array length).
     */
    [[nodiscard]] std::size_t size() const {
      return owner_ != nullptr ? node().childCount : 0;
    }

    /**
     * @brief Whether the tag has no children.
     */
    [[nodiscard]] bool empty() const {
      return size() == 0;
    }

    /**
     * @brief Child at @p index. Equivalent to @ref child.
     */
    [[nodiscard]] View operator[](std::size_t index) const {
      return child(index);
    }

    /**
     * @brief Child at @p position, or an empty view when out of bounds. O(position).
     */
    [[nodiscard]] View child(std::size_t position) const {
      const auto &entry = node();
      if (position >= entry.childCount) {
        return {};
      }
      std::uint32_t current = entry.firstChild;
      for (std::size_t index = 0; index < position; ++index) {
        current = owner_->nodes_[current].nextSibling;
      }
      return {owner_, current};
    }

    /**
     * @brief First child named @p requestedName, or an empty view when absent. O(size()).
     */
    [[nodiscard]] View find(std::string_view requestedName) const {
      for (const auto candidate : *this) {
        if (candidate.name() == requestedName) {
          return candidate;
        }
      }
      return {};
    }

    [[nodiscard]] Iterator begin() const {
      if (owner_ == nullptr || node().childCount == 0) {
        return {owner_, nbt::utils::NO_NODE};
      }
      return {owner_, node().firstChild};
    }

    [[nodiscard]] Iterator end() const {
      return {owner_, nbt::utils::NO_NODE};
    }

    /**
     * @brief Decode the payload as the given NBT type.
     *
     * Scalars decode big-endian numbers. Type::String returns a std::string_view and
     * Type::ByteArray a std::span<const std::byte> into the document bytes. Type::IntArray
     * and Type::LongArray return lazy @ref ArrayView views. Type::List and Type::Compound
     * return a container-capable View.
     *
     * @tparam t The expected NBT type.
     * @throws std::bad_variant_access if the tag type does not match @p t.
     */
    template <Type t>
    [[nodiscard]] auto as() const {
      require(t);
      if constexpr (t == Type::Byte) {
        return owner_->readNumber<std::int8_t>(node().payload);
      } else if constexpr (t == Type::Short) {
        return owner_->readNumber<std::int16_t>(node().payload);
      } else if constexpr (t == Type::Int) {
        return owner_->readNumber<std::int32_t>(node().payload);
      } else if constexpr (t == Type::Long) {
        return owner_->readNumber<std::int64_t>(node().payload);
      } else if constexpr (t == Type::Float) {
        return owner_->readNumber<float>(node().payload);
      } else if constexpr (t == Type::Double) {
        return owner_->readNumber<double>(node().payload);
      } else if constexpr (t == Type::String) {
        const auto offset = node().payload;
        const auto size = owner_->readNumber<std::uint16_t>(offset);
        return owner_->text(offset + 2, size);
      } else if constexpr (t == Type::ByteArray) {
        const auto count = owner_->readNumber<std::int32_t>(node().payload);
        return std::span<const std::byte>{owner_->data_.data() + node().payload + 4, static_cast<std::size_t>(count)};
      } else if constexpr (t == Type::IntArray) {
        return IntArrayView(owner_, node().payload);
      } else if constexpr (t == Type::LongArray) {
        return LongArrayView(owner_, node().payload);
      } else if constexpr (t == Type::List || t == Type::Compound) {
        return View(owner_, index_);
      } else {
        static_assert(false, "Invalid NBT type");
      }
    }

    /**
     * @brief Decode this subtree into an owning nbt::Tag.
     */
    [[nodiscard]] Tag materialize() const {
      return owner_->materialize(index_);
    }

  private:
    friend class NbtParser;

    View(const NbtParser *owner, std::uint32_t index) : owner_(owner), index_(index) {
    }

    [[nodiscard]] const Node &node() const {
      if (owner_ == nullptr || index_ >= owner_->nodes_.size()) {
        throw std::logic_error("invalid NBT view");
      }
      return owner_->nodes_[index_];
    }

    void require(Type expected) const {
      if (type() != expected) {
        throw std::bad_variant_access();
      }
    }

    const NbtParser *owner_{};
    const std::uint32_t index_{};
  };

  /**
   * @brief Empty document in Status::Empty.
   */
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

  /**
   * @brief Document owning an already materialized root tag, ready to @ref encode.
   */
  explicit NbtParser(nbt::Tag rootValue) {
    rootValue_ = std::move(rootValue);
    status_ = nbt::Status::Complete;
  }

  /**
   * @brief Override the maximum nesting depth limit.
   */
  void setMaxDepth(std::size_t value) {
    maxDepth_ = value;
  }

  /**
   * @brief Override the maximum elements per container limit.
   */
  void setMaxContainerElements(std::size_t value) {
    maxContainerElements_ = value;
  }

  /**
   * @brief Override the maximum total nodes limit.
   */
  void setMaxTotalNodes(std::size_t value) {
    maxTotalNodes_ = value;
  }

  /**
   * @brief Override the maximum input size limit in bytes.
   */
  void setMaxInputBytes(std::size_t value) {
    maxInputBytes_ = value;
  }

  /**
   * @brief Exchange the full state with another document.
   */
  void swap(NbtParser &nbt) noexcept {
    using std::swap;

    data_.swap(nbt.data_);
    swap(status_, nbt.status_);

    swap(rootValue_, nbt.rootValue_);
    swap(nodes_, nbt.nodes_);

    swap(encodedSize_, nbt.encodedSize_);
    swap(position_, nbt.position_);

    swap(maxDepth_, nbt.maxDepth_);
    swap(maxContainerElements_, nbt.maxContainerElements_);
    swap(maxTotalNodes_, nbt.maxTotalNodes_);
    swap(maxInputBytes_, nbt.maxInputBytes_);
  }

  /**
   * @brief Append a contiguous container fragment and revalidate the accumulated input.
   * @tparam Container A contiguous byte container.
   * @param chunk Bytes copied into the internal buffer.
   * @throws nbt::Exception if the accumulated input is malformed or exceeds a limit.
   */
  template <typename Container>
  void append(const Container &chunk, bool named = true) {
    append(asBytes(chunk), named);
  }

  /**
   * @brief Append a byte fragment and revalidate the accumulated input.
   *
   * Empty chunks are a no-op. If the accumulated input is still truncated the document
   * reports Status::NeedMoreData instead of throwing.
   *
   * @param chunk Bytes copied into the internal buffer.
   * @throws nbt::Exception if the accumulated input is malformed or exceeds a limit.
   */
  void append(std::span<const std::byte> chunk, bool named = true) {
    if (chunk.empty()) {
      return;
    }

    appendImpl(chunk);
    validate(true, named);
  }

  /**
   * @brief Reset the document to Status::Empty, discarding bytes, index and root tag.
   */
  void clear() noexcept {
    data_.reset();
    rootValue_.reset();
    status_ = Status::Empty;
    encodedSize_ = 0;
    position_ = 0;
    nodes_.clear();
  }

  /**
   * @brief Current document state.
   */
  [[nodiscard]] Status status() const noexcept {
    return status_;
  }

  /**
   * @brief Whether the input validated completely.
   */
  [[nodiscard]] bool valid() const noexcept {
    return status_ == Status::Complete;
  }

  /**
   * @brief Whether the document owns its bytes (container parse or @ref append) rather
   *        than borrowing them (span parse).
   */
  [[nodiscard]] bool ownsBytes() const noexcept {
    return data_.capacity() > 0;
  }

  /**
   * @brief Current internal or borrowed input bytes, including any trailing data beyond
   *        the validated root tag.
   */
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {data_.data(), data_.size()};
  }

  /**
   * @brief Lazy view of the root tag.
   * @throws std::logic_error if the document is empty or incomplete.
   */
  [[nodiscard]] View root() const {
    if (status_ == Status::Empty || nodes_.empty()) {
      throw std::logic_error("NBT is incomplete");
    }
    return {this, 0};
  }

  /**
   * @brief Decode the whole document into an owning nbt::Tag tree.
   * @throws std::logic_error if the document is empty or incomplete.
   */
  [[nodiscard]] Tag materialize() const {
    if (rootValue_) {
      return *rootValue_;
    }
    return root().materialize();
  }

  /**
   * @brief Encode the document to a new buffer.
   *
   * Only the valid NBT portion is written; trailing bytes accepted during parsing are
   * not reproduced. Values that cannot be encoded under their declared type are ignored:
   * list elements whose type differs from the element type are skipped (the written
   * length counts only the elements emitted), and TAG_End children inside compounds are
   * omitted.
   *
   * @param named Whether to include the root tag name (file style) or omit it (network style).
   * @return The encoded bytes.
   * @throws std::bad_alloc if the output buffer cannot grow.
   */
  [[nodiscard]] BufferT encode(bool named = true) const {
    BufferT output;
    encode(output, named);
    return output;
  }

  /**
   * @brief Append the encoded document to an existing buffer. See the @ref encode
   *        overload for the encoding policy.
   * @param output Destination buffer.
   * @param named Whether to include the root tag name (file style) or omit it (network style).
   * @throws std::bad_alloc if the output buffer cannot grow.
   */
  void encode(BufferT &output, bool named = true) const {
    if (status_ != nbt::Status::Complete && data_.empty() && nodes_.empty() && rootValue_ == std::nullopt) {
      throw nbt::Exception("incomplete data", 0);
    }

    if (rootValue_) {
      const std::size_t valueEncodedSize = encodedSize(rootValue_.value(), named);
      auto [buffer, available] = output.preallocate(valueEncodedSize, BufferUtils::growthSize(valueEncodedSize));
      BufferWriter appender{static_cast<std::byte *>(buffer), available};
      appendTag(appender, rootValue_.value(), named);
      output.postallocate(appender.written());
    } else if (nodes_) {
      const Node &rootNode = nodes_[0];
      const std::size_t headerSize = named ? 2 + static_cast<std::size_t>(rootNode.nameSize) : 0;
      const std::size_t valueEncodedSize = 1 + headerSize + (rootNode.end - rootNode.payload);
      auto [buffer, available] = output.preallocate(valueEncodedSize, BufferUtils::growthSize(valueEncodedSize));

      BufferWriter appender{static_cast<std::byte *>(buffer), available};
      appender.put(static_cast<std::byte>(rootNode.type));
      if (named) {
        appender.writeBE(rootNode.nameSize);
        appender.write(data_.data() + rootNode.nameOffset, rootNode.nameSize);
      }
      appender.write(data_.data() + rootNode.payload, rootNode.end - rootNode.payload);
      output.postallocate(appender.written());
    }
  }

  /**
   * @brief Size in bytes of @p value once encoded.
   * @param value Tag to measure.
   * @param named Whether the tag header (type id plus name) is included.
   */
  [[nodiscard]] static std::size_t encodedSize(const Tag &value, const bool named = true) {
    std::size_t size = 1;
    if (named) {
      size += stringSize(value.name);
    }

    return size + payloadSize(value);
  }

private:
  NbtParser(BufferT &buffer) : data_{std::move(buffer)} {
  }

  std::uint32_t parseNode(std::size_t depth, std::uint32_t previousSibling, bool named = true) {
    const auto begin = position_;
    const auto type = readType();

    const auto nodeIndex = beginNode(type, begin);
    if (previousSibling != nbt::utils::NO_NODE) {
      nodes_[previousSibling].nextSibling = nodeIndex;
    }

    if (named) {
      const auto nameSize = readNumber<std::uint16_t>();
      nodes_[nodeIndex].nameOffset = checkedOffset<nbt::utils::MAX_STR_SIZE>(position_);
      nodes_[nodeIndex].nameSize = nameSize;
      skip(nameSize);
    }

    parsePayload(nodeIndex, type, depth);
    return nodeIndex;
  }

  void appendImpl(std::span<const std::byte> chunk) {
    if (status_ != Status::NeedMoreData) {
      rootValue_.reset();
      status_ = Status::NeedMoreData;
    }

    data_.append(chunk.data(), chunk.data() + chunk.size_bytes());
  }

  void validate(bool atMost, bool named = true) {
    nodes_.clear();
    position_ = 0;
    try {
      if (data_.size() > maxInputBytes_) {
        throw Exception("NBT input byte limit exceeded", 0);
      }

      const auto type = readType(position_);
      if (type == Type::End) {
        throw Exception("unexpected TAG_End", position_);
      }

      parseNode(0, nbt::utils::NO_NODE, named);

      encodedSize_ = position_;
      status_ = Status::Complete;
    } catch (const NeedMoreDataException &nmEx) {
      nodes_.clear();
      position_ = 0;
      status_ = Status::NeedMoreData;
      if (atMost) {
        return;
      }
      throw nmEx;
    }
  }

  void parsePayload(std::uint32_t nodeIndex, Type type, std::size_t depth) {
    if (depth > maxDepth_) {
      throw Exception("NBT depth limit exceeded", position_);
    }
    nodes_[nodeIndex].payload = checkedOffset(position_);
    switch (type) {
    case Type::Byte:
      skip(1);
      break;
    case Type::Short:
      skip(2);
      break;
    case Type::Int:
    case Type::Float:
      skip(4);
      break;
    case Type::Long:
    case Type::Double:
      skip(8);
      break;
    case Type::String:
      skip(readNumber<std::uint16_t>());
      break;
    case Type::ByteArray:
      skipArray(1);
      break;
    case Type::IntArray:
      skipArray(4);
      break;
    case Type::LongArray:
      skipArray(8);
      break;
    case Type::List:
      parseList(nodeIndex, depth);
      break;
    case Type::Compound:
      parseCompound(nodeIndex, depth);
      break;
    case Type::End:
      throw Exception("unexpected TAG_End", position_);
    }
    nodes_[nodeIndex].end = checkedOffset(position_);
  }

  void parseList(std::uint32_t nodeIndex, std::size_t depth) {
    const auto elementType = readType();
    const auto count = readCount();
    if (elementType == Type::End && count != 0) {
      throw Exception("non-empty TAG_List uses TAG_End", position_);
    }
    auto &node = nodes_[nodeIndex];
    node.elementType = elementType;
    node.childCount = checkedOffset(count);
    node.firstChild = checkedOffset(nodes_.size());

    std::uint32_t previousSibling = nbt::utils::NO_NODE;
    for (std::size_t index = 0; index < count; ++index) {
      const auto child = beginNode(elementType, position_);
      if (previousSibling != nbt::utils::NO_NODE) {
        nodes_[previousSibling].nextSibling = child;
      }
      previousSibling = child;
      parsePayload(child, elementType, depth + 1);
    }
  }

  void parseCompound(std::uint32_t nodeIndex, std::size_t depth) {
    nodes_[nodeIndex].firstChild = checkedOffset(nodes_.size());
    std::size_t count{};
    std::uint32_t previousSibling = nbt::utils::NO_NODE;
    while (peek() != std::byte{}) {
      if (count >= maxContainerElements_) {
        throw Exception("NBT container element limit exceeded", position_);
      }
      previousSibling = parseNode(depth + 1, previousSibling);
      ++count;
    }
    skip(1);
    nodes_[nodeIndex].childCount = checkedOffset(count);
  }

  [[nodiscard]] std::uint32_t beginNode(Type type, std::size_t begin) {
    if (nodes_.size() >= maxTotalNodes_) {
      throw Exception("NBT node limit exceeded", position_);
    }
    const auto index = checkedOffset(nodes_.size());
    Node node;
    node.begin = checkedOffset(begin);
    node.type = type;
    node.firstChild = index + 1;
    node.nextSibling = nbt::utils::NO_NODE;
    nodes_.push_back(node);
    return index;
  }

  void skipArray(std::size_t width) {
    const auto count = readCount();
    if (count > nbt::utils::MAX_SIZE) {
      throw Exception("NBT array size overflow", position_);
    }
    skip(count * width);
  }

  [[nodiscard]] std::size_t readCount() {
    const auto count = readNumber<std::uint32_t>();
    if (static_cast<std::size_t>(count) > maxContainerElements_) {
      throw Exception("NBT container element limit exceeded", position_ - 4);
    }
    return static_cast<std::size_t>(count);
  }

  [[nodiscard]] Type readType(std::size_t offset) {
    const auto raw = readNumber<std::uint8_t>(offset);
    if (raw > static_cast<std::uint8_t>(Type::LongArray)) {
      throw Exception("unknown NBT type", position_ - 1);
    }
    return static_cast<Type>(raw);
  }

  [[nodiscard]] Type readType() {
    const auto raw = readNumber<std::uint8_t>();
    if (raw > static_cast<std::uint8_t>(Type::LongArray)) {
      throw Exception("unknown NBT type", position_ - 1);
    }
    return static_cast<Type>(raw);
  }

  template <class T>
  [[nodiscard]] T readNumber(std::size_t offset) const {
    if constexpr (sizeof(T) == 1) {
      return static_cast<T>(std::to_integer<std::uint8_t>(data_.data()[offset]));
    } else {
      nbt::utils::nbt_number<T> bits;
      std::memcpy(&bits, data_.data() + offset, sizeof(T));
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

  [[nodiscard]] std::byte peek() const {
    require(1);
    return data_.data()[position_];
  }

  void skip(std::size_t count) {
    require(count);
    position_ += count;
  }

  void require(std::size_t count) const {
    if (count > data_.size() - std::min(data_.size(), position_)) {
      throw NeedMoreDataException("need more data", position_);
    }
  }

  template <std::size_t MAX = nbt::utils::MAX_SIZE>
  [[nodiscard]] static std::uint32_t checkedOffset(std::size_t value) {
    if (value > MAX) {
      throw Exception("NBT offset exceeds "s + std::to_string(MAX), value);
    }
    return static_cast<std::uint32_t>(value);
  }

  [[nodiscard]] std::string_view text(std::size_t offset, std::size_t size) const {
    return {reinterpret_cast<const char *>(data_.data() + offset), size};
  }

  [[nodiscard]] Tag materialize(std::uint32_t nodeIndex) const {
    const auto &node = nodes_[nodeIndex];
    Tag value;
    value.type = node.type;
    value.name.assign(text(node.nameOffset, node.nameSize));
    switch (node.type) {
    case Type::Byte:
      value.payload = readNumber<Tag::Byte>(node.payload);
      break;
    case Type::Short:
      value.payload = readNumber<Tag::Short>(node.payload);
      break;
    case Type::Int:
      value.payload = readNumber<Tag::Int>(node.payload);
      break;
    case Type::Long:
      value.payload = readNumber<Tag::Long>(node.payload);
      break;
    case Type::Float:
      value.payload = readNumber<Tag::Float>(node.payload);
      break;
    case Type::Double:
      value.payload = readNumber<Tag::Double>(node.payload);
      break;
    case Type::String:
      value.payload = std::string(View(this, nodeIndex).template as<Type::String>());
      break;
    case Type::ByteArray: {
      const auto bytes = View(this, nodeIndex).template as<Type::ByteArray>();
      nbt::Tag::ByteArray result(bytes.size());
      std::memcpy(result.data(), bytes.data(), bytes.size());
      value.payload = std::move(result);
      break;
    }
    case Type::IntArray: {
      IntArrayView view(this, node.payload);
      nbt::Tag::IntArray result(view.size());
      for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = view[index];
      }
      value.payload = std::move(result);
      break;
    }
    case Type::LongArray: {
      LongArrayView view(this, node.payload);
      std::vector<std::int64_t> result(view.size());
      for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = view[index];
      }
      value.payload = std::move(result);
      break;
    }
    case Type::List: {
      std::vector<Tag> values;
      values.reserve(node.childCount);
      for (const auto child : View(this, nodeIndex)) {
        values.push_back(child.materialize());
      }
      value.elementType = node.elementType;
      value.payload = std::move(values);
      break;
    }
    case Type::Compound: {
      std::vector<Tag> values;
      values.reserve(node.childCount);
      for (const auto child : View(this, nodeIndex)) {
        values.push_back(child.materialize());
      }
      value.payload = std::move(values);
      break;
    }
    case Type::End:
      break;
    }
    return value;
  }

  static std::size_t stringSize(std::string_view value) noexcept {
    if (value.size() > utils::MAX_STR_SIZE) {
      return utils::MAX_STR_SIZE;
    }
    return value.size() + 2;
  }

  static std::size_t listSize(const Tag &parent, const Tag::Container &values) noexcept {
    if (values.size() > utils::MAX_SIZE - 5) {
      return utils::MAX_SIZE;
    }
    switch (parent.elementType) {
    case Type::End:
      return 5;
    case Type::Byte:
      return 5 + values.size();
    case Type::Short:
      return 5 + (values.size() * 2);
    case Type::Int:
    case Type::Float:
      return 5 + (values.size() * 4);
    case Type::Long:
    case Type::Double:
      return 5 + (values.size() * 8);
    case Type::String:
    case Type::List:
    case Type::Compound:
    case Type::ByteArray:
    case Type::IntArray:
    case Type::LongArray: {
      std::size_t size = 5;
      for (const auto &element : values) {
        if (element.type != parent.elementType) {
          continue;
        }
        const auto elementSize = payloadSize(element);
        if (elementSize > utils::MAX_SIZE - size) {
          return utils::MAX_SIZE;
        }
        size += elementSize;
      }
      return size;
    }
    }
    return 0;
  }

  [[nodiscard]] static std::size_t listElementCount(const Tag &parent, const Tag::Container &values) noexcept {
    if (parent.elementType == Type::End) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto &element : values) {
      if (element.type == parent.elementType) {
        ++count;
      }
    }
    return count;
  }

  template <typename T>
    requires std::disjunction_v<std::is_same<T, Tag::Byte>, std::is_same<T, Tag::Int>, std::is_same<T, Tag::Long>>
  static std::size_t arraySize(const std::vector<T> &values) noexcept {
    if (values.size() > utils::MAX_SIZE) {
      return utils::MAX_SIZE;
    }
    return 4 + (values.size() * sizeof(T));
  }

  static std::size_t compoundSize(const Tag::Container &values) noexcept {
    std::size_t size = 1;
    for (const auto &element : values) {
      if (element.type == Type::End) {
        continue;
      }
      const auto elementSize = encodedSize(element, true);
      if (elementSize > utils::MAX_SIZE - size) {
        return utils::MAX_SIZE;
      }
      size += elementSize;
    }
    return size;
  }

  static void appendString(BufferWriter &output, std::string_view value) {
    if (value.size() > nbt::utils::MAX_STR_SIZE) {
      throw std::length_error("NBT string exceeds 65535 bytes");
    }
    output.writeBE(static_cast<std::uint16_t>(value.size()));
    output.write(value.data(), value.size());
  }

  static void appendLength(BufferWriter &output, std::size_t size) {
    if (size > nbt::utils::MAX_SIZE) {
      throw std::length_error("NBT container is too large");
    }
    output.writeBE(static_cast<std::int32_t>(size));
  }

  static void appendTag(BufferWriter &output, const Tag &value, bool named = true) {
    if (value.type == Type::End) {
      throw std::invalid_argument("TAG_End");
    }
    output.writeBE(static_cast<std::byte>(value.type));
    if (named) {
      appendString(output, value.name);
    }
    appendPayload(output, value);
  }

  static std::size_t payloadSize(const Tag &value) noexcept {
    switch (value.type) {
    case Type::Byte:
      return 1;
    case Type::Short:
      return 2;
    case Type::Float:
    case Type::Int:
      return 4;
    case Type::Double:
    case Type::Long:
      return 8;
    case Type::String:
      return stringSize(std::get<std::string>(value.payload));
    case Type::ByteArray:
      return arraySize(std::get<Tag::ByteArray>(value.payload));
    case Type::IntArray:
      return arraySize(std::get<Tag::IntArray>(value.payload));
    case Type::LongArray:
      return arraySize(std::get<Tag::LongArray>(value.payload));
    case Type::List:
      return listSize(value, std::get<Tag::Container>(value.payload));
    case Type::Compound:
      return compoundSize(std::get<Tag::Container>(value.payload));
    default:
    case Type::End:
      return 0;
    }
  }

  static void appendPayload(BufferWriter &output, const Tag &value) {
    switch (value.type) {
    case Type::Byte:
      output.put(static_cast<std::byte>(std::get<std::int8_t>(value.payload)));
      break;
    case Type::Short:
      output.writeBE(std::get<std::int16_t>(value.payload));
      break;
    case Type::Int:
      output.writeBE(std::get<std::int32_t>(value.payload));
      break;
    case Type::Long:
      output.writeBE(std::get<std::int64_t>(value.payload));
      break;
    case Type::Float:
      output.writeBE(std::get<float>(value.payload));
      break;
    case Type::Double:
      output.writeBE(std::get<double>(value.payload));
      break;
    case Type::String:
      appendString(output, std::get<std::string>(value.payload));
      break;
    case Type::ByteArray: {
      nbt::Tag::ByteArray values;
      if (std::holds_alternative<nbt::Tag::ByteArray>(value.payload)) {
        values = std::get<nbt::Tag::ByteArray>(value.payload);
      } else if (std::holds_alternative<nbt::Tag::Container>(value.payload)) {
        const auto &tagValues = std::get<nbt::Tag::Container>(value.payload);
        values.reserve(tagValues.size());
        for (auto element : tagValues) {
          if (element.type == Type::Byte) {
            values.push_back(std::get<nbt::Tag::Byte>(element.payload));
          }
        }
      }
      appendLength(output, values.size());
      output.write(values.data(), values.size());
      break;
    }
    case Type::IntArray: {
      nbt::Tag::IntArray values;
      if (std::holds_alternative<nbt::Tag::IntArray>(value.payload)) {
        values = std::get<nbt::Tag::IntArray>(value.payload);
      } else if (std::holds_alternative<nbt::Tag::Container>(value.payload)) {
        const auto &tagValues = std::get<nbt::Tag::Container>(value.payload);
        values.reserve(tagValues.size());
        for (auto element : tagValues) {
          if (element.type == Type::Int) {
            values.push_back(std::get<nbt::Tag::Int>(element.payload));
          }
        }
      }
      appendLength(output, values.size());
      for (auto element : values) {
        output.writeBE(element);
      }
      break;
    }
    case Type::LongArray: {

      nbt::Tag::LongArray values;
      if (std::holds_alternative<nbt::Tag::LongArray>(value.payload)) {
        values = std::get<nbt::Tag::LongArray>(value.payload);
      } else if (std::holds_alternative<nbt::Tag::Container>(value.payload)) {
        const auto &tagValues = std::get<nbt::Tag::Container>(value.payload);
        values.reserve(tagValues.size());
        for (auto element : tagValues) {
          if (element.type == Type::Long) {
            values.push_back(std::get<nbt::Tag::Long>(element.payload));
          }
        }
      }
      appendLength(output, values.size());
      for (auto element : values) {
        output.writeBE(element);
      }
      break;
    }
    case Type::List: {
      const auto &listValue = std::get<Tag::Container>(value.payload);
      output.writeBE(static_cast<std::uint8_t>(value.elementType));

      nbt::Tag::Container values;
      values.reserve(listValue.size());
      for (const auto &element : listValue) {
        if (element.type != value.elementType) {
          continue;
        }
        values.push_back(element);
      }

      appendLength(output, values.size());
      for (const auto &element : values) {
        appendPayload(output, element);
      }

      break;
    }
    case Type::Compound:
      for (const auto &child : std::get<Tag::Container>(value.payload)) {
        if (child.type == Type::End) {
          break;
        }
        appendTag(output, child);
      }
      output.put(std::byte{0});
      break;
    case Type::End:
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
  [[nodiscard]] static B bitCast(V &value) {
    if constexpr (std::is_floating_point_v<T>) {
      return std::bit_cast<B>(value);
    } else {
      return static_cast<B>(value);
    }
  }

  BufferT data_;
  Status status_{Status::Empty};

  std::optional<Tag> rootValue_;
  std::vector<Node> nodes_;

  std::size_t encodedSize_{0};
  mutable std::size_t position_{0};

  /// Optiopns
  std::size_t maxDepth_{512};
  std::size_t maxContainerElements_{static_cast<std::size_t>(16U * 1024U * 1024U)};
  std::size_t maxTotalNodes_{static_cast<std::size_t>(64U * 1024U * 1024U)};
  std::size_t maxInputBytes_{static_cast<std::size_t>(1024U * 1024U * 1024U)};
};

/**
 * @brief Tag construction literals and the `name | value` naming helper.
 *
 * Numeric suffixes produce typed payloads (`_tb`, `_ts`, `_ti`, `_tl`, `_tf`, `_td`);
 * `_tgs`/`_ts` produce string tags/payloads. `"name" | value` assigns a name to a Tag.
 */
namespace tag_literals {

/**
 * @brief TAG_String literal producing an owning Tag.
 */
[[nodiscard]] constexpr Tag operator""_tgs(const char *str, size_t len) {
  return Tag::String(str, len);
}

[[nodiscard]] constexpr Tag operator""_tgb(unsigned long long value) noexcept {
  return Tag::Byte(value);
}

[[nodiscard]] constexpr Tag operator""_tgs(unsigned long long value) noexcept {
  return Tag::Short(value);
}

[[nodiscard]] constexpr Tag operator""_tgi(unsigned long long value) noexcept {
  return Tag::Int(value);
}

[[nodiscard]] constexpr Tag operator""_tgl(unsigned long long value) noexcept {
  return Tag::Long(value);
}

[[nodiscard]] constexpr Tag operator""_tgf(long double value) noexcept {
  return Tag::Float(value);
}

[[nodiscard]] constexpr Tag operator""_tgd(long double value) noexcept {
  return Tag::Double(value);
}

[[nodiscard]] constexpr Tag::String operator""_ts(const char *str, size_t len) {
  return Tag::String(str, len);
}

[[nodiscard]] constexpr Tag::Byte operator""_tb(unsigned long long value) noexcept {
  return Tag::Byte(value);
}

[[nodiscard]] constexpr Tag::Short operator""_ts(unsigned long long value) noexcept {
  return Tag::Short(value);
}

[[nodiscard]] constexpr Tag::Int operator""_ti(unsigned long long value) noexcept {
  return Tag::Int(value);
}

[[nodiscard]] constexpr Tag::Long operator""_tl(unsigned long long value) noexcept {
  return Tag::Long(value);
}

[[nodiscard]] constexpr Tag::Float operator""_tf(long double value) noexcept {
  return Tag::Float(value);
}

[[nodiscard]] constexpr Tag::Double operator""_td(long double value) noexcept {
  return Tag::Double(value);
}

/**
 * @brief Assign a name to an existing tag, returning the same reference.
 */
template <typename String>
  requires std::is_constructible_v<std::string, String &&>
[[nodiscard]] constexpr Tag &operator|(String &&name, Tag &value) {
  value.name = std::string{std::forward<String>(name)};
  return value;
}

/**
 * @brief Assign a name to a temporary tag.
 */
template <typename String>
  requires std::is_constructible_v<std::string, String &&>
[[nodiscard]] constexpr Tag operator|(String &&name, Tag &&value) {
  value.name = std::string{std::forward<String>(name)};
  return std::move(value);
}

/**
 * @brief Build a named tag from a string-like name and a Tag-convertible value.
 */
template <typename String, typename Value>
  requires(std::is_constructible_v<std::string, String &&> && std::is_convertible_v<Value &&, Tag> && !std::is_same_v<std::remove_cvref_t<Value>, Tag>)
[[nodiscard]] constexpr Tag operator|(String &&name, Value &&value) {
  return Tag(std::forward<String>(name), std::forward<Value>(value));
}

} // namespace tag_literals

/**
 * @brief Default document type using nbt::Buffer storage.
 */
using Nbt = NbtParser<nbt::Buffer>;

/**
 * @brief Alias for the lazy non-owning view exposed by nbt::Nbt.
 */
using NbtView = Nbt::View;
} // namespace nbt
