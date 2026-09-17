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

class Nbt final {
public:
  enum class Type : std::uint8_t { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

  enum class Format : std::uint8_t { File, Network };
  enum class Status : std::uint8_t { Complete, NeedMoreData };

  struct Options {
    std::size_t maxDepth{512};
    std::size_t maxContainerElements{static_cast<std::size_t>(16U * 1024U * 1024U)};
    std::size_t maxTotalNodes{static_cast<std::size_t>(64U * 1024U * 1024U)};
    std::size_t maxInputBytes{static_cast<std::size_t>(1024U * 1024U * 1024U)};
    bool requireCompleteInput{true};
    Format format{Format::File};
  };

  struct Value;

  struct List {
    Type elementType{Type::End};
    std::vector<Value> values;
  };

  struct Compound {
    std::vector<Value> values;
  };

  struct Value {
    using Payload = std::variant<std::monostate, std::int8_t, std::int16_t, std::int32_t, std::int64_t, float, double, std::string, std::vector<std::int8_t>, std::vector<std::int32_t>, std::vector<std::int64_t>, List, Compound>;

    Type type{Type::End};
    std::string name;
    Payload payload;
  };

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

    [[nodiscard]] std::size_t beginOffset() const {
      return node().begin;
    }

    [[nodiscard]] std::size_t endOffset() const {
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
      return View(owner_, current);
    }

    [[nodiscard]] View find(std::string_view requestedName) const {
      for (std::size_t index = 0; index < childCount(); ++index) {
        auto candidate = child(index);
        if (candidate.name() == requestedName) {
          return candidate;
        }
      }
      return {};
    }

    [[nodiscard]] std::int8_t asInt8() const {
      require(Type::Byte);
      return owner_->readNumber<std::int8_t>(node().payload);
    }

    [[nodiscard]] std::int16_t asInt16() const {
      require(Type::Short);
      return owner_->readNumber<std::int16_t>(node().payload);
    }

    [[nodiscard]] std::int32_t asInt32() const {
      require(Type::Int);
      return owner_->readNumber<std::int32_t>(node().payload);
    }

    [[nodiscard]] std::int64_t asInt64() const {
      require(Type::Long);
      return owner_->readNumber<std::int64_t>(node().payload);
    }

    [[nodiscard]] float asFloat32() const {
      require(Type::Float);
      return owner_->readNumber<float>(node().payload);
    }

    [[nodiscard]] double asFloat64() const {
      require(Type::Double);
      return owner_->readNumber<double>(node().payload);
    }

    [[nodiscard]] std::string_view asString() const {
      require(Type::String);
      const auto offset = node().payload;
      const auto size = owner_->readNumber<std::uint16_t>(offset);
      return owner_->text(offset + 2, size);
    }

    [[nodiscard]] std::span<const std::byte> asByteArray() const {
      require(Type::ByteArray);
      const auto count = owner_->readNumber<std::int32_t>(node().payload);
      return {owner_->data_ + node().payload + 4, static_cast<std::size_t>(count)};
    }

    [[nodiscard]] std::vector<std::int32_t> asIntArray() const {
      require(Type::IntArray);
      return std::get<std::vector<std::int32_t>>(materialize().payload);
    }

    [[nodiscard]] std::vector<std::int64_t> asLongArray() const {
      require(Type::LongArray);
      return std::get<std::vector<std::int64_t>>(materialize().payload);
    }

    [[nodiscard]] Value materialize() const {
      return owner_->materialize(index_);
    }

  private:
    friend class Nbt;

    View(const Nbt *owner, std::uint32_t index) : owner_(owner), index_(index) {
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

    const Nbt *owner_{};
    std::uint32_t index_{};
  };

  Nbt() = default;

  Nbt(Value rootValue) : rootValue_(std::move(rootValue)) {
  }

  Nbt(const Nbt &) = delete;
  Nbt &operator=(const Nbt &) = delete;
  Nbt(Nbt &&) noexcept = default;
  Nbt &operator=(Nbt &&) noexcept = default;

  [[nodiscard]] Status borrow(std::span<const std::byte> input) {
    Options options;
    return borrow(input, options);
  }

  [[nodiscard]] Status borrow(std::span<const std::byte> input, const Options &options) {
    rootValue_.reset();
    owned_.reset();
    capacity_ = 0;
    data_ = input.data();
    size_ = input.size();
    options_ = options;
    return validate();
  }

  [[nodiscard]] Status replaceBorrowed(std::span<const std::byte> input) {
    if (owned_) {
      throw std::logic_error("NBT input is owned");
    }
    data_ = input.data();
    size_ = input.size();
    return validate();
  }

  [[nodiscard]] Status take(std::unique_ptr<std::byte[]> input, std::size_t size) {
    Options options;
    return take(std::move(input), size, options);
  }

  [[nodiscard]] Status take(std::unique_ptr<std::byte[]> input, std::size_t size, const Options &options) {
    rootValue_.reset();
    owned_ = std::move(input);
    data_ = owned_.get();
    size_ = size;
    capacity_ = size;
    options_ = options;
    return validate();
  }

  [[nodiscard]] Status feed(std::span<const std::byte> chunk) {
    Options options;
    return feed(chunk, options);
  }

  [[nodiscard]] Status feed(std::span<const std::byte> chunk, const Options &options) {
    if (!accumulating_) {
      rootValue_.reset();
      options_ = options;
      accumulating_ = true;
    }
    const auto requiredCapacity = size_ + chunk.size();
    if (!owned_ || requiredCapacity > capacity_) {
      const auto grownCapacity = std::max(requiredCapacity, std::max<std::size_t>(64, capacity_ * 2));
      auto replacement = std::make_unique<std::byte[]>(grownCapacity);
      if (size_ > 0) {
        std::memcpy(replacement.get(), data_, size_);
      }
      owned_ = std::move(replacement);
      data_ = owned_.get();
      capacity_ = grownCapacity;
    }
    if (!chunk.empty()) {
      std::memcpy(owned_.get() + size_, chunk.data(), chunk.size());
    }
    size_ = requiredCapacity;
    return validate();
  }

  void clear() noexcept {
    rootValue_.reset();
    owned_.reset();
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
    encodedSize_ = 0;
    nodes_.clear();
    complete_ = false;
    accumulating_ = false;
  }

  [[nodiscard]] bool complete() const noexcept {
    return complete_;
  }

  [[nodiscard]] bool ownsBytes() const noexcept {
    return owned_ != nullptr;
  }

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {data_, size_};
  }

  [[nodiscard]] View root() const {
    if (!complete_ || nodes_.empty()) {
      throw std::logic_error("NBT is incomplete");
    }
    return View(this, 0);
  }

  [[nodiscard]] Value materialize() const {
    if (rootValue_) {
      return *rootValue_;
    }
    return root().materialize();
  }

  [[nodiscard]] std::vector<std::byte> encode(Format format = Format::File) const {
    if (complete_ && format == options_.format) {
      return {data_, data_ + encodedSize_};
    }

    Value value = materialize();
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

  [[nodiscard]] static std::size_t encodedSize(const Value &rootValue, Format format = Format::File) {
    if (format == Format::Network && rootValue.type != Type::Compound) {
      throw std::invalid_argument("Network NBT root must be TAG_Compound");
    }
    return valueSize(rootValue, format == Format::File);
  }

  [[nodiscard]] static Value int8(std::string name, std::int8_t value) {
    return {.type = Type::Byte, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Value int16(std::string name, std::int16_t value) {
    return {.type = Type::Short, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Value int32(std::string name, std::int32_t value) {
    return {.type = Type::Int, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Value int64(std::string name, std::int64_t value) {
    return {.type = Type::Long, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Value float32(std::string name, float value) {
    return {.type = Type::Float, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Value float64(std::string name, double value) {
    return {.type = Type::Double, .name = std::move(name), .payload = value};
  }

  [[nodiscard]] static Value string(std::string name, std::string value) {
    return {.type = Type::String, .name = std::move(name), .payload = std::move(value)};
  }

  [[nodiscard]] static Value byteArray(std::string name, std::vector<std::int8_t> values) {
    return {.type = Type::ByteArray, .name = std::move(name), .payload = std::move(values)};
  }

  [[nodiscard]] static Value intArray(std::string name, std::vector<std::int32_t> values) {
    return {.type = Type::IntArray, .name = std::move(name), .payload = std::move(values)};
  }

  [[nodiscard]] static Value longArray(std::string name, std::vector<std::int64_t> values) {
    return {.type = Type::LongArray, .name = std::move(name), .payload = std::move(values)};
  }

  [[nodiscard]] static Value list(std::string name, Type elementType, std::vector<Value> values = {}) {
    return {.type = Type::List, .name = std::move(name), .payload = List{.elementType = elementType, .values = std::move(values)}};
  }

  [[nodiscard]] static Value compound(std::string name, std::vector<Value> values = {}) {
    return {.type = Type::Compound, .name = std::move(name), .payload = Compound{std::move(values)}};
  }

private:
  class NeedMore final {};

  [[nodiscard]] Status validate() {
    nodes_.clear();
    complete_ = false;
    position_ = 0;
    try {
      if (size_ > options_.maxInputBytes) {
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
      if (options_.requireCompleteInput && position_ != size_) {
        throw Error("trailing NBT data", position_);
      }
      complete_ = true;
      return Status::Complete;
    } catch (const NeedMore &) {
      nodes_.clear();
      position_ = 0;
      return Status::NeedMoreData;
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
      bits = static_cast<Bits>((bits << 8) | std::to_integer<std::uint8_t>(data_[offset + index]));
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
    return data_[position_];
  }

  void skip(std::size_t count) {
    require(count);
    position_ += count;
  }

  void require(std::size_t count) const {
    if (count > size_ - std::min(size_, position_)) {
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
    return {reinterpret_cast<const char *>(data_ + offset), size};
  }

  [[nodiscard]] Value materialize(std::uint32_t nodeIndex) const {
    const auto &node = nodes_[nodeIndex];
    Value value;
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
      value.payload = std::string(View(this, nodeIndex).asString());
      break;
    case Type::ByteArray: {
      const auto bytes = View(this, nodeIndex).asByteArray();
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
      List list{node.elementType, {}};
      list.values.reserve(node.childCount);
      for (std::size_t index = 0; index < node.childCount; ++index) {
        list.values.push_back(View(this, nodeIndex).child(index).materialize());
      }
      value.payload = std::move(list);
      break;
    }
    case Type::Compound: {
      Compound compound;
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

  static void appendNamed(std::vector<std::byte> &output, const Value &value) {
    if (value.type == Type::End) {
      throw std::invalid_argument("named TAG_End");
    }
    appendNumber(output, static_cast<std::uint8_t>(value.type));
    appendString(output, value.name);
    appendPayload(output, value);
  }

  static void appendPayload(std::vector<std::byte> &output, const Value &value) {
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
      const auto &listValue = std::get<List>(value.payload);
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
      for (const auto &child : std::get<Compound>(value.payload).values) {
        appendNamed(output, child);
      }
      output.push_back(std::byte{});
      break;
    case Type::End:
      break;
    }
  }

  [[nodiscard]] static std::size_t valueSize(const Value &value, bool named) {
    std::vector<std::byte> temporary;
    if (named) {
      appendNamed(temporary, value);
    } else {
      appendPayload(temporary, value);
    }
    return temporary.size();
  }

  static constexpr std::uint32_t noNode = (std::numeric_limits<std::uint32_t>::max)();
  std::unique_ptr<std::byte[]> owned_;
  std::optional<Value> rootValue_;
  const std::byte *data_{};
  std::size_t size_{};
  std::size_t capacity_{};
  std::size_t encodedSize_{};
  mutable std::size_t position_{};
  Options options_;
  std::vector<Node> nodes_;
  bool complete_{};
  bool accumulating_{};
};

} // namespace nbt
