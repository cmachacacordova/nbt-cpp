/**
 * @file nbt.h
 * @author Carlos Machaca (carloscordova96@hotmail.com)
 * @brief
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

class Error : public std::runtime_error {
public:
  Error(const std::string &message, std::size_t offset) : std::runtime_error(message), offset_(offset) {
  }

  [[nodiscard]] std::size_t offset() const noexcept {
    return offset_;
  }

private:
  std::size_t offset_;
};

enum class Type : std::uint8_t { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

enum class Source : std::uint8_t { File, Network };

enum class Status : std::uint8_t { Empty, Complete, NeedMoreData, Error };

struct Options {
  std::size_t maxDepth{512};
  std::size_t maxContainerElements{static_cast<std::size_t>(16U * 1024U * 1024U)};
  std::size_t maxTotalNodes{static_cast<std::size_t>(64U * 1024U * 1024U)};
  std::size_t maxInputBytes{static_cast<std::size_t>(1024U * 1024U * 1024U)};
  bool requireCompleteInput{true};
  Source format{Source::File};
};

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

  Type type{Type::End};
  Type elementType{Type::End};
  std::string name;
  Payload payload;

  Tag() = default;

  template <typename T>
  Tag(T value) : Tag("", std::move(value)) {
  }

  template <typename Name, typename T>
    requires std::is_constructible_v<std::string, Name &&>
  Tag(Name &&name, T value) : name{std::forward<Name>(name)} {
    using disjunction = std::disjunction<std::is_same<T, Byte>, std::is_same<T, Short>, std::is_same<T, Int>, std::is_same<T, Long>, std::is_same<T, Float>, std::is_same<T, Double>, std::is_same<T, String>, std::is_same<T, Tag>>;
    static_assert(disjunction::value, "Type is not constructible from Tag");

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
    } else if constexpr (std::is_same_v<T, String>) {
      this->payload = std::move(value);
      this->type = Type::String;
    } else if constexpr (std::is_same_v<T, Tag>) {
      this->payload = value.payload;
      this->type = value.type;
      this->elementType = value.elementType;
    }
  }

  template <typename T>
  Tag(std::vector<T> elements) : Tag("", std::move(elements)) {
  }

  template <typename Name, typename T>
    requires std::is_constructible_v<std::string, Name &&>
  Tag(Name &&name, std::vector<T> elements) {
    using disjunction = std::disjunction<std::is_same<T, Byte>, std::is_same<T, Short>, std::is_same<T, Int>, std::is_same<T, Long>, std::is_same<T, Float>, std::is_same<T, Double>, std::is_same<T, String>, std::is_same<T, Tag>>;
    static_assert(disjunction::value, "Type is not constructible from Tag");
    this->name = std::string{std::forward<Name>(name)};
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
    } else {
      this->type = Type::List;
      this->elementType = Type::List;
    }
  }

  Tag(std::string name, Type type, std::vector<Tag> value) {
    this->name = std::move(name);
    this->payload = std::move(value);
    this->type = Type::List;
    this->elementType = type;
  }
};

template <typename BufferT = nbt::Buffer>
class NbtParser final {
public:
  [[nodiscard]] static NbtParser parse(std::span<const std::byte> data) {
    Options options;
    return parse(data, options);
  }

  [[nodiscard]] static NbtParser parse(std::span<const std::byte> data, const Options &options) {
    nbt::NbtParser<BufferT> document;

    if (data.size() == 0) {
      document.status_ = Status::NeedMoreData;
      return document;
    }

    BufferT bytes(data);

    document.data_.swap(bytes);
    document.encodedSize_ = 0;
    document.options_ = options;
    document.validate();

    return document;
  }

  template <typename Container>
  [[nodiscard]] static NbtParser parse(const Container &data) {
    Options options;
    return parse(data, options);
  }

  template <typename Container>
  [[nodiscard]] static NbtParser parse(const Container &data, const Options &options) {
    if (data.empty()) {
      NbtParser document;
      document.status_ = Status::NeedMoreData;
      return document;
    }

    const std::span<const std::byte> view = asBytes(data);

    BufferT bytes;
    bytes.append(view.data(), view.data() + view.size_bytes());

    nbt::NbtParser<BufferT> document;
    document.data_.swap(bytes);
    document.encodedSize_ = 0;
    document.options_ = options;
    document.validate();

    return document;
  }

private:
  struct Node {
    std::uint32_t begin{};
    std::uint32_t end{};
    std::uint32_t payload{};
    std::uint32_t nameOffset{};
    std::uint32_t firstChild{};
    std::uint32_t nextSibling{};
    std::uint32_t childCount{};
    std::uint16_t nameSize{};
    Type type{Type::End};
    Type elementType{Type::End};
  };

public:
  template <typename T>
  class ArrayView {
  public:
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

    [[nodiscard]] std::size_t size() const noexcept {
      return owner_ ? static_cast<std::size_t>(owner_->readNumber<std::int32_t>(payload_)) : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
      return size() == 0;
    }

    [[nodiscard]] T operator[](std::size_t index) const {
      return owner_->readNumber<T>(payload_ + 4 + sizeof(T) * index);
    }

    [[nodiscard]] T front() const {
      return (*this)[0];
    }

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

  using IntArrayView = ArrayView<Tag::Int>;
  using LongArrayView = ArrayView<Tag::Long>;

  class View {
  public:
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

    [[nodiscard]] explicit operator bool() const noexcept {
      return owner_ != nullptr;
    }

    [[nodiscard]] Type type() const {
      return node().type;
    }

    [[nodiscard]] Type elementType() const {
      return owner_ != nullptr ? node().elementType : Type::End;
    }

    [[nodiscard]] std::size_t payloadBegin() const {
      return node().begin;
    }

    [[nodiscard]] std::size_t payloadEnd() const {
      return node().end;
    }

    [[nodiscard]] std::string_view name() const {
      const auto &entry = node();
      return owner_->text(entry.nameOffset, entry.nameSize);
    }

    [[nodiscard]] std::size_t size() const {
      return owner_ != nullptr ? node().childCount : 0;
    }

    [[nodiscard]] bool empty() const {
      return size() == 0;
    }

    [[nodiscard]] View operator[](std::size_t index) const {
      return child(index);
    }

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

    [[nodiscard]] View find(std::string_view requestedName) const {
      const size_t count = this->size();
      for (std::size_t index = 0; index < count; ++index) {
        auto candidate = child(index);
        if (candidate.name() == requestedName) {
          return candidate;
        }
      }
      return {};
    }

    [[nodiscard]] Iterator begin() const {
      if (owner_ == nullptr || node().childCount == 0) {
        return {owner_, noNode};
      }
      return {owner_, node().firstChild};
    }

    [[nodiscard]] Iterator end() const {
      return {owner_, noNode};
    }

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

  NbtParser(const NbtParser &) = delete;

  NbtParser &operator=(const NbtParser &) = delete;

  NbtParser() = default;

  NbtParser(Tag rootValue) : NbtParser() {
    rootValue_ = std::move(rootValue);
  }

  NbtParser(NbtParser &&other) noexcept {
    this->swap(other);
  }

  NbtParser &operator=(NbtParser &&other) noexcept {
    this->swap(other);
    return *this;
  }

  void swap(NbtParser &nbt) noexcept {
    using std::swap;

    data_.swap(nbt.data_);
    swap(rootValue_, nbt.rootValue_);
    swap(options_, nbt.options_);
    swap(status_, nbt.status_);
    swap(encodedSize_, nbt.encodedSize_);
    swap(position_, nbt.position_);
    swap(nodes_, nbt.nodes_);
  }

  template <typename Container>
  void append(const Container &chunk) {
    Options options;
    append(chunk, options);
  }

  template <typename Container>
  void append(const Container &chunk, const Options &options) {
    append(asBytes(chunk), options);
  }

  void append(std::span<const std::byte> chunk) {
    Options options;
    append(chunk, options);
  }

  void append(std::span<const std::byte> chunk, const Options &options) {
    if (chunk.empty()) {
      return;
    }

    if (status_ != Status::NeedMoreData) {
      rootValue_.reset();
      options_ = options;
      status_ = Status::NeedMoreData;
    }

    if (chunk.size_bytes() > 0) [[likely]] {
      data_.append(chunk.data(), chunk.data() + chunk.size_bytes());
    }

    validate();
  }

  void clear() noexcept {
    data_.reset();
    rootValue_.reset();
    status_ = Status::Empty;
    encodedSize_ = 0;
    position_ = 0;
    nodes_.clear();
  }

  [[nodiscard]] Status status() const noexcept {
    return status_;
  }

  [[nodiscard]] bool complete() const noexcept {
    return status_ == Status::Complete;
  }

  [[nodiscard]] bool ownsBytes() const noexcept {
    return data_.capacity() > 0;
  }

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {data_.data(), data_.size()};
  }

  [[nodiscard]] View root() const {
    if (status_ == Status::Error) {
      throw std::logic_error("NBT is invalid");
    }
    if (status_ == Status::Empty || nodes_.empty()) {
      throw std::logic_error("NBT is incomplete");
    }
    return {this, 0};
  }

  [[nodiscard]] Tag materialize() const {
    if (rootValue_) {
      return *rootValue_;
    }
    return root().materialize();
  }

  [[nodiscard]] BufferT encode(Source format = Source::File) const {
    BufferT output;
    encode(output, format);
    return output;
  }

  void encode(BufferT &output, Source format = Source::File) const {
    if (complete() && format == options_.format) {
      auto [buffer, size] = output.preallocate(encodedSize_, BufferUtils::growthSize(encodedSize_));
      if (buffer == nullptr) {
        throw std::bad_alloc();
      }
      std::memcpy(buffer, data_.data(), encodedSize_);
      output.postallocate(encodedSize_);
      return;
    }

    Tag value = materialize();
    if (format == Source::Network && value.type != Type::Compound) {
      throw std::invalid_argument("Network NBT root must be TAG_Compound");
    }

    validateRootTag(value, format);

    const std::size_t valueEncodedSize = encodedSize(value, format == Source::File);
    auto [buffer, available] = output.preallocate(valueEncodedSize, BufferUtils::growthSize(valueEncodedSize));
    if (buffer == nullptr) {
      throw std::bad_alloc();
    }

    BufferWriter appender{static_cast<std::byte *>(buffer), available};

    if (format == Source::Network) {
      appendNumber(appender, static_cast<std::uint8_t>(Type::Compound));
      appendPayload(appender, value);
    } else {
      appendNamed(appender, value);
    }
    output.postallocate(appender.written());
  }

  static void validateRootTag(const Tag &root, const Source source = Source::File) {
    if (source == Source::Network && root.type != Type::Compound) {
      throw std::invalid_argument("Network NBT root must be TAG_Compound");
    }
    if (root.type == Type::End) {
      throw std::invalid_argument("Tag is TAG_End");
    }
  }

  [[nodiscard]] static std::size_t encodedSize(const Tag &value, const bool named = true) {
    std::size_t size = 1;
    if (named) {
      size += stringSize(value.name);
    }

    return size + payloadSize(value);
  }

private:
  class NeedMore final {};

  void validate() {
    nodes_.clear();
    position_ = 0;
    try {
      if (data_.size() > options_.maxInputBytes) {
        throw Error("NBT input byte limit exceeded", 0);
      }
      if (options_.format == Source::File) {
        parseNamed(noNode, 0, noNode);
      } else {
        const auto begin = position_;
        const auto type = readType();
        if (type != Type::Compound) {
          throw Error("Network NBT root must be TAG_Compound", begin);
        }
        const auto rootIndex = beginNode(type, begin, noNode);
        parsePayload(rootIndex, type, 0);
      }
      encodedSize_ = position_;
      if (options_.requireCompleteInput && position_ != data_.size()) {
        throw Error("trailing NBT data", position_);
      }
      status_ = Status::Complete;
    } catch (const NeedMore &) {
      nodes_.clear();
      position_ = 0;
      status_ = Status::NeedMoreData;
    }
  }

  std::uint32_t parseNamed(std::uint32_t parent, std::size_t depth, std::uint32_t previousSibling) {
    const auto begin = position_;
    const auto type = readType();
    if (type == Type::End) {
      throw Error("named TAG_End", begin);
    }
    const auto nodeIndex = beginNode(type, begin, parent);
    if (previousSibling != noNode) {
      nodes_[previousSibling].nextSibling = nodeIndex;
    }
    const auto nameSize = readNumber<std::uint16_t>();
    nodes_[nodeIndex].nameOffset = checkedOffset(position_);
    nodes_[nodeIndex].nameSize = nameSize;
    skip(nameSize);
    parsePayload(nodeIndex, type, depth);
    return nodeIndex;
  }

  void parsePayload(std::uint32_t nodeIndex, Type type, std::size_t depth) {
    if (depth > options_.maxDepth) {
      throw Error("NBT depth limit exceeded", position_);
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
      throw Error("unexpected TAG_End", position_);
    }
    nodes_[nodeIndex].end = checkedOffset(position_);
  }

  void parseList(std::uint32_t nodeIndex, std::size_t depth) {
    const auto elementType = readType();
    const auto count = readCount();
    if (elementType == Type::End && count != 0) {
      throw Error("non-empty TAG_List uses TAG_End", position_);
    }
    auto &node = nodes_[nodeIndex];
    node.elementType = elementType;
    node.childCount = checkedOffset(count);
    node.firstChild = checkedOffset(nodes_.size());
    std::uint32_t previousSibling = noNode;
    for (std::size_t index = 0; index < count; ++index) {
      const auto child = beginNode(elementType, position_, nodeIndex);
      if (previousSibling != noNode) {
        nodes_[previousSibling].nextSibling = child;
      }
      previousSibling = child;
      parsePayload(child, elementType, depth + 1);
    }
  }

  void parseCompound(std::uint32_t nodeIndex, std::size_t depth) {
    nodes_[nodeIndex].firstChild = checkedOffset(nodes_.size());
    std::size_t count{};
    std::uint32_t previousSibling = noNode;
    while (peek() != std::byte{}) {
      if (count >= options_.maxContainerElements) {
        throw Error("NBT container element limit exceeded", position_);
      }
      previousSibling = parseNamed(nodeIndex, depth + 1, previousSibling);
      ++count;
    }
    skip(1);
    nodes_[nodeIndex].childCount = checkedOffset(count);
  }

  [[nodiscard]] std::uint32_t beginNode(Type type, std::size_t begin, std::uint32_t parent) {
    if (nodes_.size() >= options_.maxTotalNodes) {
      throw Error("NBT node limit exceeded", position_);
    }
    const auto index = checkedOffset(nodes_.size());
    Node node;
    node.begin = checkedOffset(begin);
    node.type = type;
    node.firstChild = index + 1;
    node.nextSibling = noNode;
    nodes_.push_back(node);
    (void)parent;
    return index;
  }

  void skipArray(std::size_t width) {
    const auto count = readCount();
    if (count > (std::numeric_limits<std::size_t>::max)() / width) {
      throw Error("NBT array size overflow", position_);
    }
    skip(count * width);
  }

  [[nodiscard]] std::size_t readCount() {
    const auto count = readNumber<std::int32_t>();
    if (count < 0) {
      throw Error("negative NBT length", position_ - 4);
    }
    if (static_cast<std::size_t>(count) > options_.maxContainerElements) {
      throw Error("NBT container element limit exceeded", position_ - 4);
    }
    return static_cast<std::size_t>(count);
  }

  [[nodiscard]] Type readType() {
    const auto raw = readNumber<std::uint8_t>();
    if (raw > static_cast<std::uint8_t>(Type::LongArray)) {
      throw Error("unknown NBT type", position_ - 1);
    }
    return static_cast<Type>(raw);
  }

  template <class T>
  [[nodiscard]] T readNumber(std::size_t offset) const {
    using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    Bits bits{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      bits = static_cast<Bits>((bits << 8) | std::to_integer<std::uint8_t>(data_.data()[offset + index]));
    }
    if constexpr (std::is_floating_point_v<T>) {
      return std::bit_cast<T>(bits);
    } else {
      return static_cast<T>(bits);
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
      throw NeedMore{};
    }
  }

  [[nodiscard]] static std::uint32_t checkedOffset(std::size_t value) {
    if (value > (std::numeric_limits<std::uint32_t>::max)()) {
      throw Error("NBT offset exceeds 32-bit index", value);
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
      std::vector<std::int8_t> result(bytes.size());
      std::memcpy(result.data(), bytes.data(), bytes.size());
      value.payload = std::move(result);
      break;
    }
    case Type::IntArray: {
      IntArrayView view(this, node.payload);
      std::vector<std::int32_t> result(view.size());
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
      for (std::size_t index = 0; index < node.childCount; ++index) {
        values.push_back(View(this, nodeIndex).child(index).materialize());
      }
      value.elementType = node.elementType;
      value.payload = std::move(values);
      break;
    }
    case Type::Compound: {
      std::vector<Tag> values;
      values.reserve(node.childCount);
      for (std::size_t index = 0; index < node.childCount; ++index) {
        values.push_back(View(this, nodeIndex).child(index).materialize());
      }
      value.payload = std::move(values);
      break;
    }
    case Type::End:
      break;
    }
    return value;
  }

  template <class T>
  static void appendNumber(BufferWriter &output, T value) {
    using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    const Bits bits = [&] {
      if constexpr (std::is_floating_point_v<T>) {
        return std::bit_cast<Bits>(value);
      } else {
        return static_cast<Bits>(value);
      }
    }();
    for (std::size_t index = sizeof(T); index > 0; --index) {
      output.put(static_cast<std::byte>((bits >> ((index - 1) * 8)) & 0xff));
    }
  }

  static std::size_t stringSize(std::string_view value) {
    if (value.size() > (std::numeric_limits<std::uint16_t>::max)()) {
      throw std::length_error("NBT string exceeds 65535 bytes");
    }
    return value.size() + 2;
  }

  static std::size_t listSize(const Tag &parent, const std::vector<Tag> &values) {
    if (values.size() > (std::numeric_limits<std::uint32_t>::max)()) {
      throw std::length_error("NBT container exceeds 4294967295 bytes");
    }
    size_t size = 5;
    switch (parent.elementType) {
    case Type::Byte: {
      size += values.size();
      break;
    }
    case Type::Short: {
      size += values.size() * 2;
      break;
    }
    case Type::Float:
    case Type::Int: {
      size += values.size() * 4;
      break;
    }
    case Type::Double:
    case Type::Long: {
      size += values.size() * 8;
      break;
    }
    case Type::List:
    case Type::Compound:
    case Type::String: {
      for (const auto &element : values) {
        if (element.type != parent.elementType || !element.name.empty()) {
          throw std::invalid_argument("invalid TAG_List element");
        }
        size += payloadSize(element);
      }
    }
    default:
      break;
    }
    return size;
  }

  template <typename T>
  static std::size_t arraySize(const std::vector<T> &values) {
    using disjunction = std::disjunction<std::is_same<T, Tag::Byte>, std::is_same<T, Tag::Int>, std::is_same<T, Tag::Long>>;
    static_assert(disjunction::value, "T is not supported as array or list");
    if (values.size() > (std::numeric_limits<std::uint32_t>::max)()) {
      throw std::length_error("NBT container exceeds 4294967295 bytes");
    }
    return (values.size() * sizeof(T)) + 4;
  }

  static std::size_t compoundSize(const std::vector<Tag> &values) {
    std::size_t size = 1;
    for (const auto &element : values) {
      size += encodedSize(element, true);
    }
    return size;
  }

  static void appendString(BufferWriter &output, std::string_view value) {
    if (value.size() > (std::numeric_limits<std::uint16_t>::max)()) {
      throw std::length_error("NBT string exceeds 65535 bytes");
    }
    appendNumber(output, static_cast<std::uint16_t>(value.size()));
    output.write(value.data(), value.size());
  }

  static void appendLength(BufferWriter &output, std::size_t size) {
    if (size > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
      throw std::length_error("NBT container is too large");
    }
    appendNumber(output, static_cast<std::int32_t>(size));
  }

  static void appendNamed(BufferWriter &output, const Tag &value) {
    if (value.type == Type::End) {
      throw std::invalid_argument("named TAG_End");
    }
    appendNumber(output, static_cast<std::uint8_t>(value.type));
    appendString(output, value.name);
    appendPayload(output, value);
  }

  static std::size_t payloadSize(const Tag &value) {
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
      appendNumber(output, std::get<std::int8_t>(value.payload));
      break;
    case Type::Short:
      appendNumber(output, std::get<std::int16_t>(value.payload));
      break;
    case Type::Int:
      appendNumber(output, std::get<std::int32_t>(value.payload));
      break;
    case Type::Long:
      appendNumber(output, std::get<std::int64_t>(value.payload));
      break;
    case Type::Float:
      appendNumber(output, std::get<float>(value.payload));
      break;
    case Type::Double:
      appendNumber(output, std::get<double>(value.payload));
      break;
    case Type::String:
      appendString(output, std::get<std::string>(value.payload));
      break;
    case Type::ByteArray: {
      const auto &values = std::get<std::vector<std::int8_t>>(value.payload);
      appendLength(output, values.size());
      output.write(values.data(), values.size());
      break;
    }
    case Type::IntArray: {
      const auto &values = std::get<std::vector<std::int32_t>>(value.payload);
      appendLength(output, values.size());
      for (auto element : values) {
        appendNumber(output, element);
      }
      break;
    }
    case Type::LongArray: {
      const auto &values = std::get<std::vector<std::int64_t>>(value.payload);
      appendLength(output, values.size());
      for (auto element : values) {
        appendNumber(output, element);
      }
      break;
    }
    case Type::List: {
      const auto &listValue = std::get<Tag::Container>(value.payload);
      appendNumber(output, static_cast<std::uint8_t>(value.elementType));
      appendLength(output, listValue.size());
      for (const auto &element : listValue) {
        if (element.type != value.elementType || !element.name.empty()) {
          throw std::invalid_argument("invalid TAG_List element");
        }
        appendPayload(output, element);
      }
      break;
    }
    case Type::Compound:
      for (const auto &child : std::get<Tag::Container>(value.payload)) {
        appendNamed(output, child);
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

  static constexpr std::uint32_t noNode = (std::numeric_limits<std::uint32_t>::max)();

  BufferT data_;

  std::optional<Tag> rootValue_;
  Options options_;
  Status status_{Status::Empty};
  std::size_t encodedSize_{0};
  mutable std::size_t position_{0};
  std::vector<Node> nodes_;
};

namespace tag_literals {

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

template <typename String>
  requires std::is_constructible_v<std::string, String &&>
[[nodiscard]] constexpr Tag &operator|(String &&name, Tag &value) {
  value.name = std::string{std::forward<String>(name)};
  return value;
}

template <typename String>
  requires std::is_constructible_v<std::string, String &&>
[[nodiscard]] constexpr Tag operator|(String &&name, Tag &&value) {
  value.name = std::string{std::forward<String>(name)};
  return value;
}

template <typename String, typename Value>
  requires(std::is_constructible_v<std::string, String &&> && std::is_convertible_v<Value &&, Tag> && !std::is_same_v<std::remove_cvref_t<Value>, Tag>)
[[nodiscard]] constexpr Tag operator|(String &&name, Value &&value) {
  return Tag(std::forward<String>(name), std::forward<Value>(value));
}

} // namespace tag_literals

using Nbt = NbtParser<nbt::Buffer>;

using NbtView = Nbt::View;
} // namespace nbt
