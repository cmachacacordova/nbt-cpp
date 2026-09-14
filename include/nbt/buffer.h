#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace nbt {

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
    for (const auto *ring = other.head(); ring != nullptr; ring = ring->next.get()) {
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

} // namespace nbt
