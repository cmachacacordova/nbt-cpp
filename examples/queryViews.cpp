#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace nbt::tag_literals;
  const auto bytes = Nbt(Tag("player", std::vector<Tag>{Tag("health", 20_i), Tag("name", "Alex"_s)})).encode();
  Nbt document = Nbt::parse(bytes);
  if (document.status() != Status::Complete) {
    return 1;
  }
  const auto health = document.root().find("health");
  if (!health || health.as<Type::Int>() != 20) {
    return 1;
  }
  return health.payloadBegin() < health.payloadEnd() ? 0 : 1;
}
