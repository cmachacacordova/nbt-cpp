#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

namespace nbt {

namespace BufferUtils {

[[nodiscard]] std::size_t growthSize(std::size_t needed) noexcept {
  return ((needed / 64) + 1) * 64;
}

} // namespace BufferUtils

class Buffer {
private:
  inline static std::allocator<std::byte> byteAlloc{};

  std::byte *data_{};
  std::size_t size_{0};
  std::size_t capacity_{0};

public:
  Buffer() = default;

  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  Buffer(Buffer &&other) noexcept : data_{other.data_}, size_{other.size_}, capacity_{other.capacity_} {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  Buffer &operator=(Buffer &&other) noexcept {
    if (this != &other) {
      reset();
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      other.data_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
    }
    return *this;
  }

  Buffer(std::span<const std::byte> view) : data_{const_cast<std::byte *>(view.data())}, size_{view.size_bytes()} {
  }

  explicit Buffer(std::size_t capacity) {
    const auto initial = std::max<std::size_t>(64, capacity);
    data_ = std::allocator_traits<std::allocator<std::byte>>::allocate(byteAlloc, initial);
    capacity_ = initial;
  }

  std::pair<void *, std::size_t> preallocate(std::size_t min, std::size_t newAllocationSize = 64, std::size_t max = std::numeric_limits<std::size_t>::max()) {
    const auto tailroom = capacity_ - size_;
    if (min <= tailroom) {
      return {data_ + size_, std::min(max, tailroom)};
    }

    if (capacity_ >= max) {
      return {nullptr, 0};
    }

    const auto additional = std::max(min, newAllocationSize);
    if (additional > max - capacity_) {
      return {nullptr, 0};
    }

    const auto grownCapacity = capacity_ + additional;
    std::byte *replacement = std::allocator_traits<std::allocator<std::byte>>::allocate(byteAlloc, grownCapacity);
    if (size_ > 0) {
      std::memcpy(replacement, data_, size_);
    }
    if (capacity_ > 0) {
      std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
    }

    data_ = replacement;
    capacity_ = grownCapacity;

    return {data_ + size_, std::min(max, capacity_ - size_)};
  }

  void postallocate(std::size_t n) noexcept {
    size_ += n;
  }

  void append(const std::byte *begin, const std::byte *end) {
    if (begin == end) {
      return;
    }
    const auto length = static_cast<std::size_t>(end - begin);
    auto [buffer, available] = preallocate(length, BufferUtils::growthSize(length));
    if (buffer == nullptr) [[unlikely]] {
      throw std::bad_alloc{};
    }
    std::memcpy(buffer, begin, length);
    postallocate(length);
  }

  void swap(Buffer &other) noexcept {
    using std::swap;
    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
  }

  void reset() noexcept {
    if (capacity_ > 0) {
      std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
    }
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
  }

  [[nodiscard]] const std::byte *data() const noexcept {
    return data_;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return capacity_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] const std::byte *begin() const noexcept {
    return data_;
  }

  [[nodiscard]] const std::byte *end() const noexcept {
    return data_ + size_;
  }

  ~Buffer() {
    if (capacity_ > 0) {
      std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
    }
  }
};

class BufferWriter final {
public:
  BufferWriter(std::byte *data, std::size_t capacity) : begin_(data), current_(data), end_(data + capacity) {
  }

  void put(std::byte value) {
    if (current_ == end_) [[unlikely]] {
      throw std::overflow_error("overflow");
    }
    *current_++ = value;
  }

  void write(const void *data, std::size_t length) {
    if (static_cast<std::size_t>(end_ - current_) < length) [[unlikely]] {
      throw std::overflow_error("overflow");
    }
    if (length > 0) [[likely]] {
      std::memcpy(current_, data, length);
      current_ += length;
    }
  }

  void write(std::span<const std::byte> data) {
    write(data.data(), data.size_bytes());
  }

  [[nodiscard]] std::size_t written() const noexcept {
    return static_cast<std::size_t>(current_ - begin_);
  }

private:
  std::byte *begin_{};
  std::byte *current_{};
  std::byte *end_{};
};

} // namespace nbt
