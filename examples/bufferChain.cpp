#include <span>

#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  const auto bytes = Nbt(Nbt::compound("root", {Nbt::int32("answer", 42)})).encode();
  Nbt document;
  const auto split = bytes.size() / 2;
  if (document.append(std::span(bytes).first(split)); document.status() != Nbt::Status::NeedMoreData) {
    return 1;
  }
  if (document.append(std::span(bytes).subspan(split)); document.status() != Nbt::Status::Complete) {
    return 1;
  }
  return document.root().find("answer").as<Nbt::Type::Int>() == 42 ? 0 : 1;
}
