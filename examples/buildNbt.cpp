#include "nbt/nbt.h"

int main() {
  using NBTTag = nbt::Tag;
  using NbtParser = nbt::NbtParser;
  using namespace nbt::tag_literals;
  using namespace std::string_literals;

  auto tag = "name"s | "Alex"_ts;

  NbtParser document(NBTTag(NBTTag::Compound{tag, {"answer", 42_ti}}));
  return document.encode("root").empty() ? 1 : 0;
}
