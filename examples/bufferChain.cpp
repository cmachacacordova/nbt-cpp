#include <span>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace nbt::tag_literals;
  const auto bytes = Nbt(Tag("root"_s, Tag::Container{Tag("answer"_s, 42_i)})).encode();
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
