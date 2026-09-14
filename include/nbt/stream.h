#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "nbt/buffer.h"
#include "nbt/error.h"
#include "nbt/export.h"
#include "nbt/tag.h"
#include "nbt/token.h"
#include "nbt/type.h"

namespace nbt {

// Forward declaration used by InputStream.
class InputStream;

class NBT_CPP_API InputStream {
public:
  virtual ~InputStream() = default;

  [[nodiscard]] virtual std::size_t position() const = 0;
  [[nodiscard]] virtual std::size_t remaining() const = 0;
  virtual void require(std::size_t n) = 0;
  virtual void readBytes(std::byte *dest, std::size_t n) = 0;
  [[nodiscard]] virtual std::byte peekByte() = 0;

  std::byte readByte() {
    std::byte value;
    readBytes(&value, 1);
    return value;
  }
};

[[nodiscard]] NBT_CPP_API Tag parse(InputStream &stream, const ParseOptions &options);
[[nodiscard]] NBT_CPP_API TokenizedDocument tokenize(std::span<const std::byte> input, const ParseOptions &options);

class NBT_CPP_API StreamParser {
public:
  StreamParser() = default;

  StreamParser &feed(std::span<const std::byte> chunk) {
    pending_.append(chunk);
    return *this;
  }

  StreamParser &feed(const void *data, std::size_t size) {
    pending_.append(data, size);
    return *this;
  }

  StreamParser &feed(const std::vector<std::byte> &vec) {
    pending_.append(vec);
    return *this;
  }

  StreamParser &feed(Buffer buffer) {
    pending_.append(buffer);
    return *this;
  }

  [[nodiscard]] bool empty() const noexcept {
    return pending_.empty();
  }

  [[nodiscard]] std::size_t available() const noexcept {
    return pending_.size();
  }

  [[nodiscard]] const Buffer &pending() const noexcept {
    return pending_;
  }

  [[nodiscard]] Tag parse(const ParseOptions &options = defaultStreamOptions()) {
    Tag result = parseInternal(options);
    return result;
  }

  [[nodiscard]] std::optional<Tag> tryParse(const ParseOptions &options = defaultStreamOptions()) {
    try {
      return parseInternal(options);
    } catch (const IncompleteDataError &) {
      return std::nullopt;
    }
  }

  [[nodiscard]] TokenizedDocument tokenize(const ParseOptions &options = defaultStreamOptions()) {
    return nbt::tokenize(pending_.contiguous(), options);
  }

  [[nodiscard]] std::optional<TokenizedDocument> tryTokenize(const ParseOptions &options = defaultStreamOptions()) {
    try {
      return nbt::tokenize(pending_.contiguous(), options);
    } catch (const IncompleteDataError &) {
      return std::nullopt;
    }
  }

private:
  class PendingStream : public InputStream {
  public:
    explicit PendingStream(const Buffer &buffer) : buffer_(buffer), current_(buffer.head()), pos_(0), offsetInChunk_(0) {
    }

    [[nodiscard]] std::size_t position() const override {
      return pos_;
    }

    [[nodiscard]] std::size_t remaining() const override {
      return buffer_.size() - pos_;
    }

    void require(std::size_t n) override {
      if (n > remaining()) {
        throw IncompleteDataError("incomplete stream data", pos_);
      }
    }

    void readBytes(std::byte *dest, std::size_t n) override {
      require(n);
      while (n > 0) {
        advance();
        const auto available = current_->data.size() - offsetInChunk_;
        const auto take = std::min(available, n);
        std::memcpy(dest, current_->data.data() + offsetInChunk_, take);
        offsetInChunk_ += take;
        pos_ += take;
        dest += take;
        n -= take;
      }
    }

    [[nodiscard]] std::byte peekByte() override {
      require(1);
      advance();
      return current_->data[offsetInChunk_];
    }

  private:
    void advance() {
      while (current_ != nullptr && offsetInChunk_ >= current_->data.size()) {
        offsetInChunk_ = 0;
        current_ = current_->next.get();
      }
      if (current_ == nullptr) {
        throw IncompleteDataError("incomplete stream data", pos_);
      }
    }

    const Buffer &buffer_;
    const Buffer::Ring *current_;
    std::size_t pos_;
    std::size_t offsetInChunk_;
  };

  [[nodiscard]] static const ParseOptions &defaultStreamOptions() {
    static ParseOptions options;
    options.requireCompleteInput = false;
    return options;
  }

  [[nodiscard]] Tag parseInternal(const ParseOptions &options) {
    PendingStream stream(pending_);
    Tag result = nbt::parse(stream, options);
    pending_.consume(stream.position());
    return result;
  }

  Buffer pending_;
};

} // namespace nbt
