#include <cstdint>
#include <span>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace std::string_literals;
  Nbt source(Tag("root", std::vector<Tag>{Tag("health", std::int32_t(20)), Tag("name", "Alex"s)}));
  const auto encoded = source.encode();
  const auto firstHalf = std::span(encoded).first(encoded.size() / 2);
  Nbt document = Nbt::parse(firstHalf);
  if (document.status() != Status::NeedMoreData) {
    return 1;
  }
  document = Nbt::parse(encoded);
  if (document.status() != Status::Complete) {
    return 1;
  }
  return document.root().find("health").as<Type::Int>() == 20 ? 0 : 1;
}
