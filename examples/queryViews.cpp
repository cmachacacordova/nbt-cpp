#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace std::string_literals;
  const auto bytes = Nbt(Tag("player", std::vector<Tag>{Tag("health", int32_t(20)), Tag("name", "Alex"s)})).encode();
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
