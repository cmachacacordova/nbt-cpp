#include <span>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  Nbt source(Tag::compound("root", {Tag::int32("health", 20), Tag::string("name", "Alex")}));
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
