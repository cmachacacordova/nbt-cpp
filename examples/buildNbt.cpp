#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  Nbt document(Nbt::compound("root", {Nbt::int32("answer", 42), Nbt::string("name", "Alex")}));
  return document.encode().empty() ? 1 : 0;
}
