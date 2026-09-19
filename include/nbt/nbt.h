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
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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

enum class BufferType : std::uint8_t { Raw, View };

enum class Type : std::uint8_t { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

enum class Format : std::uint8_t { File, Network };

enum class Status : std::uint8_t { Empty, Complete, NeedMoreData, Error };

struct Options {
  std::size_t maxDepth{512};
  std::size_t maxContainerElements{static_cast<std::size_t>(16U * 1024U * 1024U)};
  std::size_t maxTotalNodes{static_cast<std::size_t>(64U * 1024U * 1024U)};
  std::size_t maxInputBytes{static_cast<std::size_t>(1024U * 1024U * 1024U)};
  bool requireCompleteInput{true};
  Format format{Format::File};
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

  struct List {
    Type elementType{Type::End};
    std::vector<Tag> values; ///< List elements in serialized order.

    List() = default;

    /**
     * @brief Constructs a list by taking ownership of existing elements.
     *
     * @param value
     */
    explicit List(Type elementType, std::vector<Tag> value) : elementType(elementType), values(std::move(value)) {
    }

    /**
     * @brief Constructs a list from an initializer list.
     *
     * @param init
     */
    List(std::initializer_list<Tag> init) : values(init) {
    }
  };

  /** @brief Ordered collection of named child tags. */
  struct Compound {
    std::vector<Tag> values; ///< Child values in serialized order.

    Compound() = default;

    /**
     * @brief Constructs a compound by taking ownership of existing children.
     *
     * @param value
     */
    explicit Compound(std::vector<Tag> value) : values(std::move(value)) {
    }

    /**
     * @brief Constructs a compound from an initializer list.
     *
     * @param init
     */
    Compound(std::initializer_list<Tag> init) : values(init) {
    }
  };

  using Payload = std::variant<std::monostate, Byte, Short, Int, Long, Float, Double, String, List, Compound, ByteArray, IntArray, LongArray>;

  Type type{Type::End};
  std::string name;
  Payload payload;

  [[nodiscard]] static Tag int8(std::string name, std::int8_t value) {
    return {.type = Type::Byte, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Tag int16(std::string name, std::int16_t value) {
    return {.type = Type::Short, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Tag int32(std::string name, std::int32_t value) {
    return {.type = Type::Int, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Tag int64(std::string name, std::int64_t value) {
    return {.type = Type::Long, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Tag float32(std::string name, float value) {
    return {.type = Type::Float, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Tag float64(std::string name, double value) {
    return {.type = Type::Double, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Tag string(std::string name, std::string value) {
    return {.type = Type::String, .name = std::move(name), .payload = std::move(value)};
  }

  [[nodiscard]] static Tag byteArray(std::string name, std::vector<std::int8_t> values) {
    return {.type = Type::ByteArray, .name = std::move(name), .payload = std::move(values)};
  }

  [[nodiscard]] static Tag intArray(std::string name, std::vector<std::int32_t> values) {
    return {.type = Type::IntArray, .name = std::move(name), .payload = std::move(values)};
  }

  [[nodiscard]] static Tag longArray(std::string name, std::vector<std::int64_t> values) {
    return {.type = Type::LongArray, .name = std::move(name), .payload = std::move(values)};
  }

  [[nodiscard]] static Tag list(std::string name, Type type, std::vector<Tag> values = {}) {
    return {.type = Type::List, .name = std::move(name), .payload = Tag::List(type, std::move(values))};
  }

  [[nodiscard]] static Tag compound(std::string name, std::vector<Tag> values = {}) {
    return {.type = Type::Compound, .name = std::move(name), .payload = Tag::Compound{std::move(values)}};
  }
};

class Buffer {
private:
  inline static std::allocator<std::byte> byteAlloc{};

  std::byte *data_;
  std::size_t size_;
  std::size_t capacity_;

  BufferType type_;

public:
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  Buffer(Buffer &&) noexcept = default;
  Buffer &operator=(Buffer &&) noexcept = default;

  Buffer() : data_{nullptr}, size_{0}, capacity_{0}, type_{BufferType::View} {
  }

  Buffer(std::span<const std::byte> view) : Buffer() {
    data_ = const_cast<std::byte *>(view.data());
    size_ = view.size_bytes();
    type_ = BufferType::View;
  }

  Buffer(std::size_t capacity) : Buffer() {
    try {
      const auto grownCapacity = std::max<std::size_t>({static_cast<unsigned long long>(64), capacity});
      data_ = std::allocator_traits<std::allocator<std::byte>>::allocate(byteAlloc, grownCapacity);
      size_ = 0;
      capacity_ = grownCapacity;
      type_ = BufferType::Raw;
    } catch (const std::bad_alloc &e) {
      data_ = nullptr;
      size_ = capacity_ = 0;
      type_ = BufferType::View;
    }
  }

  std::pair<void *, std::size_t> preallocate(std::size_t min, std::size_t newAllocationSize, std::size_t max = std::numeric_limits<std::size_t>::max()) {
    if (min < capacity_) {
      return std::make_pair(data_ + size_, capacity_);
    }

    if (capacity_ >= max) {
      return std::make_pair(nullptr, 0);
    }

    const auto grownCapacity = capacity_ + newAllocationSize;
    std::byte *replacement = std::allocator_traits<std::allocator<std::byte>>::allocate(byteAlloc, grownCapacity);

    if (size_ > 0) {
      std::memcpy(replacement, data_, size_);
      if (type_ == BufferType::Raw) {
        std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
        data_ = nullptr;
      }
    }

    capacity_ = grownCapacity - size_;
    data_ = replacement;
    type_ = BufferType::Raw;

    return std::make_pair(data_ + size_, capacity_);
  }

  void postallocate(std::size_t n) noexcept {
    capacity_ -= n;
    size_ += n;
  }

  void append(const std::byte *begin, const std::byte *end) {
    auto [buffer, size] = preallocate(static_cast<std::size_t>(end - begin), ((static_cast<size_t>(end - begin) / 64) + 1) * 64);
    if (begin != nullptr && begin != end && begin < end) [[likely]] {
      std::memcpy(static_cast<std::byte *>(buffer), begin, static_cast<std::size_t>(end - begin));
      postallocate(static_cast<std::size_t>(end - begin));
    }
  }

  void swap(Buffer &other) noexcept {
    using std::swap;
    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
    swap(type_, other.type_);
  }

  void reset() noexcept {
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
    type_ = BufferType::View;
  }

  [[nodiscard]] std::byte *data() const {
    return data_;
  }

  [[nodiscard]] std::size_t size() const {
    return size_;
  }

  [[nodiscard]] std::size_t capacity() const {
    return capacity_;
  }

  ~Buffer() {
    if (type_ != BufferType::View) {
      std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
    }
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

    Buffer bytes(data);

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
    std::span<const std::byte> view = std::as_bytes(std::span(data));

    if (view.empty()) {
      NbtParser document;
      document.status_ = Status::NeedMoreData;
      return document;
    }

    Buffer bytes;
    auto [buffer, size] = bytes.preallocate(view.size_bytes(), view.size_bytes() * 2);
    if (buffer == nullptr) [[unlikely]] {
      nbt::NbtParser<BufferT> document;
      document.status_ = Status::Error;
      return document;
    }
    std::memcpy(buffer, view.data(), size);
    bytes.postallocate(view.size());

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
    std::uint32_t subtreeEnd{};
    std::uint32_t childCount{};
    std::uint16_t nameSize{};
    std::string name;
    Type type{Type::End};
    Type elementType{Type::End};
  };

public:
  class View {
  public:
    View() = default;

    [[nodiscard]] explicit operator bool() const noexcept {
      return owner_ != nullptr;
    }

    [[nodiscard]] Type type() const {
      return node().type;
    }

    [[nodiscard]] std::size_t begin() const {
      return node().begin;
    }

    [[nodiscard]] std::size_t end() const {
      return node().end;
    }

    [[nodiscard]] std::string_view name() const {
      const auto &entry = node();
      return owner_->text(entry.nameOffset, entry.nameSize);
    }

    [[nodiscard]] std::size_t childCount() const {
      return node().childCount;
    }

    [[nodiscard]] View child(std::size_t position) const {
      const auto &entry = node();
      if (position >= entry.childCount) {
        return {};
      }
      std::uint32_t current = entry.firstChild;
      for (std::size_t index = 0; index < position; ++index) {
        current = owner_->nodes_[current].subtreeEnd;
      }
      return {owner_, current};
    }

    [[nodiscard]] View find(std::string_view requestedName) const {
      const size_t childCount = this->childCount();
      for (std::size_t index = 0; index < childCount; ++index) {
        auto candidate = child(index);
        if (candidate.name() == requestedName) {
          return candidate;
        }
      }
      return {};
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
        return std::get<std::vector<std::int32_t>>(materialize().payload);
      } else if constexpr (t == Type::LongArray) {
        return std::get<std::vector<std::int64_t>>(materialize().payload);
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
    append(std::as_bytes(chunk), options);
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

  [[nodiscard]] std::vector<std::byte> encode(Format format = Format::File) const {
    if (complete() && format == options_.format) {
      return {data_.data(), data_.data() + encodedSize_};
    }

    Tag value = materialize();
    if (format == Format::Network && value.type != Type::Compound) {
      throw std::invalid_argument("Network NBT root must be TAG_Compound");
    }
    size_t size = encodedSize(value, format);

    std::vector<std::byte> output;
    output.reserve(size);

    if (format == Format::Network) {
      appendNumber(output, static_cast<std::uint8_t>(Type::Compound));
      appendPayload(output, value);
    } else {
      appendNamed(output, value);
    }

    return output;
  }

  [[nodiscard]] static std::size_t encodedSize(const Tag &rootValue, Format format = Format::File) {
    if (format == Format::Network && rootValue.type != Type::Compound) {
      throw std::invalid_argument("Network NBT root must be TAG_Compound");
    }
    size_t size = valueSize(rootValue, format == Format::File);
    return format == Format::Network ? size + 1 : size;
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
      if (options_.format == Format::File) {
        parseNamed(noNode, 0);
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

  void parseNamed(std::uint32_t parent, std::size_t depth) {
    const auto begin = position_;
    const auto type = readType();
    if (type == Type::End) {
      throw Error("named TAG_End", begin);
    }
    const auto nodeIndex = beginNode(type, begin, parent);
    const auto nameSize = readNumber<std::uint16_t>();
    nodes_[nodeIndex].nameOffset = checkedOffset(position_);
    nodes_[nodeIndex].nameSize = nameSize;
    skip(nameSize);
    parsePayload(nodeIndex, type, depth);
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
    nodes_[nodeIndex].subtreeEnd = checkedOffset(nodes_.size());
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
    for (std::size_t index = 0; index < count; ++index) {
      const auto child = beginNode(elementType, position_, nodeIndex);
      parsePayload(child, elementType, depth + 1);
    }
  }

  void parseCompound(std::uint32_t nodeIndex, std::size_t depth) {
    nodes_[nodeIndex].firstChild = checkedOffset(nodes_.size());
    std::size_t count{};
    while (peek() != std::byte{}) {
      if (count >= options_.maxContainerElements) {
        throw Error("NBT container element limit exceeded", position_);
      }
      parseNamed(nodeIndex, depth + 1);
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
    node.subtreeEnd = index + 1;
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
      value.payload = readNumber<std::int8_t>(node.payload);
      break;
    case Type::Short:
      value.payload = readNumber<std::int16_t>(node.payload);
      break;
    case Type::Int:
      value.payload = readNumber<std::int32_t>(node.payload);
      break;
    case Type::Long:
      value.payload = readNumber<std::int64_t>(node.payload);
      break;
    case Type::Float:
      value.payload = readNumber<float>(node.payload);
      break;
    case Type::Double:
      value.payload = readNumber<double>(node.payload);
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
      const auto count = readNumber<std::int32_t>(node.payload);
      std::vector<std::int32_t> result(static_cast<std::size_t>(count));
      auto offset = static_cast<std::size_t>(node.payload) + 4;
      for (auto &element : result) {
        element = readNumber<std::int32_t>(offset);
        offset += 4;
      }
      value.payload = std::move(result);
      break;
    }
    case Type::LongArray: {
      const auto count = readNumber<std::int32_t>(node.payload);
      std::vector<std::int64_t> result(static_cast<std::size_t>(count));
      auto offset = static_cast<std::size_t>(node.payload) + 4;
      for (auto &element : result) {
        element = readNumber<std::int64_t>(offset);
        offset += 8;
      }
      value.payload = std::move(result);
      break;
    }
    case Type::List: {
      Tag::List list(node.elementType, {});
      list.values.reserve(node.childCount);
      for (std::size_t index = 0; index < node.childCount; ++index) {
        list.values.push_back(View(this, nodeIndex).child(index).materialize());
      }
      value.payload = std::move(list);
      break;
    }
    case Type::Compound: {
      Tag::Compound compound;
      compound.values.reserve(node.childCount);
      for (std::size_t index = 0; index < node.childCount; ++index) {
        compound.values.push_back(View(this, nodeIndex).child(index).materialize());
      }
      value.payload = std::move(compound);
      break;
    }
    case Type::End:
      break;
    }
    return value;
  }

  template <class T>
  static void appendNumber(std::vector<std::byte> &output, T value) {
    using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    const Bits bits = [&] {
      if constexpr (std::is_floating_point_v<T>) {
        return std::bit_cast<Bits>(value);
      } else {
        return static_cast<Bits>(value);
      }
    }();
    for (std::size_t index = sizeof(T); index > 0; --index) {
      output.push_back(static_cast<std::byte>((bits >> ((index - 1) * 8)) & 0xff));
    }
  }

  static size_t stringSize(std::string_view value) {
    if (value.size() > (std::numeric_limits<std::uint16_t>::max)()) {
      throw std::length_error("NBT string exceeds 65535 bytes");
    }
    return std::span(value).size_bytes() + 2;
  }

  static void appendString(std::vector<std::byte> &output, std::string_view value) {
    if (value.size() > (std::numeric_limits<std::uint16_t>::max)()) {
      throw std::length_error("NBT string exceeds 65535 bytes");
    }
    appendNumber(output, static_cast<std::uint16_t>(value.size()));
    const auto bytes = std::as_bytes(std::span(value));
    output.insert(output.end(), bytes.begin(), bytes.end());
  }

  static void appendLength(std::vector<std::byte> &output, std::size_t size) {
    if (size > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
      throw std::length_error("NBT container is too large");
    }
    appendNumber(output, static_cast<std::int32_t>(size));
  }

  static std::size_t namedSize(const Tag &value) {
    if (value.type == Type::End) {
      throw std::invalid_argument("named TAG_End");
    }
    std::size_t size = stringSize(value.name);
    size += payloadSize(value);
    size++;
    return size;
  }

  static void appendNamed(std::vector<std::byte> &output, const Tag &value) {
    if (value.type == Type::End) {
      throw std::invalid_argument("named TAG_End");
    }
    appendNumber(output, static_cast<std::uint8_t>(value.type));
    appendString(output, value.name);
    appendPayload(output, value);
  }

  static std::size_t payloadSize(const Tag &value) {
    std::size_t size = 0;
    switch (value.type) {
    case Type::Byte:
      size++;
      break;
    case Type::Short:
      size += 2;
      break;
    case Type::Float:
    case Type::Int:
      size += 4;
      break;
    case Type::Double:
    case Type::Long:
      size += 8;
      break;
    case Type::String:
      size += stringSize(std::get<std::string>(value.payload));
      break;
    case Type::ByteArray: {
      const auto &values = std::get<std::vector<std::int8_t>>(value.payload);
      size += 4;
      const auto bytes = std::as_bytes(std::span(values));
      size += bytes.size_bytes();
      break;
    }
    case Type::IntArray: {
      const auto &values = std::get<std::vector<std::int32_t>>(value.payload);
      size += 4;
      const auto bytes = std::as_bytes(std::span(values));
      size += bytes.size_bytes();
      break;
    }
    case Type::LongArray: {
      const auto &values = std::get<std::vector<std::int64_t>>(value.payload);
      size += 4;
      const auto bytes = std::as_bytes(std::span(values));
      size += bytes.size_bytes();
      break;
    }
    case Type::List: {
      const auto &listValue = std::get<Tag::List>(value.payload);
      size += 5;
      for (const auto &element : listValue.values) {
        if (element.type != listValue.elementType || !element.name.empty()) {
          throw std::invalid_argument("invalid TAG_List element");
        }
        size += payloadSize(element);
      }
      break;
    }
    case Type::Compound:
      for (const auto &child : std::get<Tag::Compound>(value.payload).values) {
        size += namedSize(child);
      }
      size++;
      break;
    case Type::End:
      break;
    }
    return size;
  }

  static void appendPayload(std::vector<std::byte> &output, const Tag &value) {
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
      const auto bytes = std::as_bytes(std::span(values));
      output.insert(output.end(), bytes.begin(), bytes.end());
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
      const auto &listValue = std::get<Tag::List>(value.payload);
      appendNumber(output, static_cast<std::uint8_t>(listValue.elementType));
      appendLength(output, listValue.values.size());
      for (const auto &element : listValue.values) {
        if (element.type != listValue.elementType || !element.name.empty()) {
          throw std::invalid_argument("invalid TAG_List element");
        }
        appendPayload(output, element);
      }
      break;
    }
    case Type::Compound:
      for (const auto &child : std::get<Tag::Compound>(value.payload).values) {
        appendNamed(output, child);
      }
      output.push_back(std::byte{});
      break;
    case Type::End:
      break;
    }
  }

  [[nodiscard]] static std::size_t valueSize(const Tag &value, bool named) {
    std::size_t size = 0;
    if (named) {
      size += namedSize(value);
    } else {
      size += payloadSize(value);
    }
    return size;
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

using Nbt = NbtParser<nbt::Buffer>;

using NbtView = Nbt::View;
} // namespace nbt
