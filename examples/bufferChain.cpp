#include <span>

#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  const auto bytes = Nbt(Nbt::compound("root", {Nbt::int32("answer", 42)})).encode();
  Nbt document;
  const auto split = bytes.size() / 2;
  if (document.feed(std::span(bytes).first(split)) != Nbt::Status::NeedMoreData) {
    return 1;
  }
  if (document.feed(std::span(bytes).subspan(split)) != Nbt::Status::Complete) {
    return 1;
  }
  return document.root().find("answer").as<Nbt::Type::Int>() == 42 ? 0 : 1;
}
