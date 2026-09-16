#pragma once

#include <string>
#include <vector>

#include "nbt/export.h"
#include "nbt/tag.h"
#include "nbt/type.h"

namespace nbt {

/** Fluent builder for an owning compound-root NBT tree. */
class NBT_CPP_API Builder {
public:
  /** Starts a builder with an empty root compound. */
  explicit Builder(std::string rootName = {});

  /** Adds a tag to the current compound or list. */
  Builder &add(Tag tag);

  /** Adds a compound and makes it the current container. */
  Builder &beginCompound(std::string name);

  /** Adds a list and makes it the current container. */
  Builder &beginList(std::string name, Type elementType);

  /** Closes the current container and selects its parent. */
  Builder &end();

  /** Returns a copy of the completed tree. */
  [[nodiscard]] Tag build() const;

private:
  Tag root_;
  std::vector<Tag *> stack_;
};

} // namespace nbt
