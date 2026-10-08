#include <span>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace nbt::tag_literals;

  const auto bytes = NbtParser(Tag(Tag::Compound{{"answer", Tag(42_ti)}})).encode(std::nullopt);
  NbtParser document;
  const auto split = bytes.size() / 2;
  if (document.append(std::span(bytes).first(split)); document.status() != Status::NeedMoreData) {
    return 1;
  }
  if (document.append(std::span(bytes).subspan(split)); document.status() != Status::Complete) {
    return 1;
  }
  auto tag = document.readTag();
  const auto *answer = tag.find("answer");
  return answer != nullptr && std::get<Tag::Int>(answer->payload()) == 42 ? 0 : 1;
}
