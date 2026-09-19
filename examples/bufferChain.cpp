#include <span>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  const auto bytes = Nbt(Tag::compound("root", {Tag::int32("answer", 42)})).encode();
  Nbt document;
  const auto split = bytes.size() / 2;
  if (document.append(std::span(bytes).first(split)); document.status() != Status::NeedMoreData) {
    return 1;
  }
  if (document.append(std::span(bytes).subspan(split)); document.status() != Status::Complete) {
    return 1;
  }
  return document.root().find("answer").as<Type::Int>() == 42 ? 0 : 1;
}
