#pragma once

#include <string>
#include <vector>

#include "nbt/export.h"
#include "nbt/tag.h"
#include "nbt/type.h"

namespace nbt {

class NBT_CPP_API Builder {
public:
  explicit Builder(std::string rootName = {});
  Builder &add(Tag tag);
  Builder &beginCompound(std::string name);
  Builder &beginList(std::string name, Type elementType);
  Builder &end();
  [[nodiscard]] Tag build() const;

private:
  Tag root_;
  std::vector<Tag *> stack_;
};

} // namespace nbt
