/**
 * @file buffer.h
 * @author Carlos Machaca (carloscordova96@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

namespace nbt {

namespace BufferUtils {

[[nodiscard]] inline std::size_t growthSize(std::size_t needed) noexcept {
  constexpr std::size_t blockSize = 64;
  const auto remainder = needed % blockSize;
  if (remainder == 0) {
    return needed;
  }
  const auto increment = blockSize - remainder;
  if (needed > std::numeric_limits<std::size_t>::max() - increment) {
    return std::numeric_limits<std::size_t>::max();
  }
  return needed + increment;
}

} // namespace BufferUtils

class Buffer {
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

  Buffer &operator=(std::span<const std::byte> &other) noexcept {
    if (this->data_ != other.data()) {
      reset();
      data_ = const_cast<std::byte *>(other.data());
      size_ = other.size_bytes();
    }
    return *this;
  }

  explicit Buffer(std::size_t capacity) {
    const auto initial = std::max<std::size_t>(64, capacity);
    data_ = std::allocator_traits<std::allocator<std::byte>>::allocate(byteAlloc, initial);
    capacity_ = initial;
  }

  ~Buffer() {
    if (capacity_ > 0) {
      std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
    }
  }

  std::pair<void *, std::size_t> preallocate(std::size_t min, std::size_t newAllocationSize = 64, std::size_t max = std::numeric_limits<std::size_t>::max()) {
    const auto allocatorMax = std::allocator_traits<std::allocator<std::byte>>::max_size(byteAlloc);
    const auto effectiveMax = std::min(max, allocatorMax);
    if (min == 0 || size_ > effectiveMax || min > effectiveMax - size_) {
      return {nullptr, 0};
    }

    const auto requiredSize = size_ + min;
    if (requiredSize <= capacity_) {
      return {data_ + size_, std::min(capacity_, effectiveMax) - size_};
    }

    const auto baseCapacity = std::max(capacity_, size_);
    const auto availableGrowth = effectiveMax - baseCapacity;
    const auto geometricGrowth = baseCapacity / 2;
    const auto preferredGrowth = std::max<std::size_t>({min, newAllocationSize, geometricGrowth});
    const auto grownCapacity = baseCapacity + std::min(preferredGrowth, availableGrowth);
    if (grownCapacity < requiredSize) {
      return {nullptr, 0};
    }

    std::byte *replacement = std::allocator_traits<std::allocator<std::byte>>::allocate(byteAlloc, grownCapacity);
    if (replacement == nullptr) {
      throw std::bad_alloc();
    }

    if (size_ > 0) {
      std::memcpy(replacement, data_, size_);
    }
    if (capacity_ > 0) {
      std::allocator_traits<std::allocator<std::byte>>::deallocate(byteAlloc, data_, capacity_);
    }

    data_ = replacement;
    capacity_ = grownCapacity;

    return {data_ + size_, std::min(capacity_ - size_, effectiveMax - size_)};
  }

  void postallocate(std::size_t n) {
    if (n > (capacity_ - std::min(capacity_, size_))) [[unlikely]] {
      throw std::overflow_error("buffer postallocation exceeds capacity");
    }
    size_ += n;
  }

  void append(const std::byte *begin, const std::byte *end) {
    if (begin == end) {
      return;
    }
    const auto less = std::less<const std::byte *>{};
    if (begin == nullptr || end == nullptr || less(end, begin)) [[unlikely]] {
      throw std::invalid_argument("invalid buffer append range");
    }

    const auto length = static_cast<std::size_t>(end - begin);
    if (capacity_ > 0) {
      const auto *storageEnd = data_ + capacity_;
      if (less(begin, storageEnd) && less(data_, end)) [[unlikely]] {
        throw std::invalid_argument("buffer append source overlaps storage");
      }
    }

    auto [buffer, available] = preallocate(length, BufferUtils::growthSize(length));
    if (buffer == nullptr || available < length) [[unlikely]] {
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
    return size_ == 0 ? data_ : data_ + size_;
  }

private:
  inline static std::allocator<std::byte> byteAlloc{};

  std::byte *data_{};
  std::size_t size_{0};
  std::size_t capacity_{0};
};

} // namespace nbt
