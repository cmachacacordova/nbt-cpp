#include "nbt/nbt.h"

int main() {
  using NBTTag = nbt::Tag;
  using Nbt = nbt::Nbt;
  using namespace nbt::tag_literals;

  NBTTag tag = "name" | "Alex"_ts;

  Nbt document(NBTTag("root", std::vector<NBTTag>{"answer"_ts | 42_ti, tag}));
  return document.encode().empty() ? 1 : 0;
}
