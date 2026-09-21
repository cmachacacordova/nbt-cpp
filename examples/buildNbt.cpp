#include "nbt/nbt.h"

int main() {
  using NBTTag = nbt::Tag;
  using Nbt = nbt::Nbt;
  using namespace nbt::tag_literals;

  NBTTag tag = "name" | "Alex"_s;

  Nbt document(NBTTag("root", std::vector<NBTTag>{"answer"_s | 42_i, tag}));
  return document.encode().empty() ? 1 : 0;
}
