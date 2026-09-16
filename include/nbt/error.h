#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "nbt/export.h"

namespace nbt {

/** Error produced by malformed NBT or an invalid library operation. */
class NBT_CPP_API Error : public std::runtime_error {
public:
  /** Constructs an error associated with a byte or character offset. */
  Error(std::string message, std::size_t offset);

  /** Returns the offset at which the error was detected. */
  [[nodiscard]] std::size_t offset() const noexcept;

private:
  std::size_t offset_;
};

/** Indicates that a stream ended before a complete NBT document was available. */
class NBT_CPP_API IncompleteDataError : public Error {
public:
  /** Constructs an incomplete-data error at the last readable offset. */
  IncompleteDataError(std::string message, std::size_t offset) : Error(std::move(message), offset) {
  }
};

} // namespace nbt
