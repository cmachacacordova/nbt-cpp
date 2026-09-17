#include <span>

#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  Nbt source(Nbt::compound("root", {Nbt::int32("health", 20), Nbt::string("name", "Alex")}));
  const auto encoded = source.encode();
  Nbt document;
  const auto firstHalf = std::span(encoded).first(encoded.size() / 2);
  if (document.borrow(firstHalf) != Nbt::Status::NeedMoreData) {
    return 1;
  }
  if (document.replaceBorrowed(encoded) != Nbt::Status::Complete) {
    return 1;
  }
  return document.root().find("health").asInt32() == 20 ? 0 : 1;
}
