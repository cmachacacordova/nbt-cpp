#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  const auto bytes = Nbt(Nbt::compound("player", {Nbt::int32("health", 20), Nbt::string("name", "Alex")})).encode();
  Nbt document = Nbt::parse(bytes);
  if (document.status() != Nbt::Status::Complete) {
    return 1;
  }
  const auto health = document.root().find("health");
  if (!health || health.as<Nbt::Type::Int>() != 20) {
    return 1;
  }
  return health.begin() < health.end() ? 0 : 1;
}
