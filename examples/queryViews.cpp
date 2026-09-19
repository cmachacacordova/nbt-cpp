#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  const auto bytes = Nbt(Tag::compound("player", {Tag::int32("health", 20), Tag::string("name", "Alex")})).encode();
  Nbt document = Nbt::parse(bytes);
  if (document.status() != Status::Complete) {
    return 1;
  }
  const auto health = document.root().find("health");
  if (!health || health.as<Type::Int>() != 20) {
    return 1;
  }
  return health.begin() < health.end() ? 0 : 1;
}
