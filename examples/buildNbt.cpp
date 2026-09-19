#include "nbt/nbt.h"

int main() {
  using NBTTag = nbt::Tag;
  using Nbt = nbt::Nbt;
  Nbt document(NBTTag::compound("root", {NBTTag::int32("answer", 42), NBTTag::string("name", "Alex")}));
  return document.encode().empty() ? 1 : 0;
}
