#include <span>

#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  Nbt source(Nbt::compound("root", {Nbt::int32("health", 20), Nbt::string("name", "Alex")}));
  const auto encoded = source.encode();
  const auto firstHalf = std::span(encoded).first(encoded.size() / 2);
  Nbt document = Nbt::parse(firstHalf);
  if (document.status() != Nbt::Status::NeedMoreData) {
    return 1;
  }
  document.reset(std::span(encoded));
  if (document.status() != Nbt::Status::Complete) {
    return 1;
  }
  return document.root().find("health").as<Nbt::Type::Int>() == 20 ? 0 : 1;
}
