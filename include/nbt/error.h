#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "nbt/export.h"

namespace nbt {

class NBT_CPP_API Error : public std::runtime_error {
public:
  Error(std::string message, std::size_t offset);
  [[nodiscard]] std::size_t offset() const noexcept;

private:
  std::size_t offset_;
};

class NBT_CPP_API IncompleteDataError : public Error {
public:
  IncompleteDataError(std::string message, std::size_t offset) : Error(std::move(message), offset) {
  }
};

} // namespace nbt
