#include <cstdint>

#include "nbt/nbt.h"

int main() {
  using NBTTag = nbt::Tag;
  using Nbt = nbt::Nbt;
  using namespace std::string_literals;

  Nbt document(NBTTag("root", std::vector<NBTTag>{NBTTag("answer", int32_t(42)), NBTTag("name", "Alex"s)}));
  return document.encode().empty() ? 1 : 0;
}
