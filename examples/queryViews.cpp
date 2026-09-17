#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  const auto bytes = Nbt(Nbt::compound("player", {Nbt::int32("health", 20), Nbt::string("name", "Alex")})).encode();
  Nbt document;
  if (document.borrow(bytes) != Nbt::Status::Complete) {
    return 1;
  }
  const auto health = document.root().find("health");
  if (!health || health.asInt32() != 20) {
    return 1;
  }
  return health.beginOffset() < health.endOffset() ? 0 : 1;
}
