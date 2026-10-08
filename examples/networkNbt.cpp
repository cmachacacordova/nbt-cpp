#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace nbt::tag_literals;
  NbtParser source(Tag(Tag::Compound{{"answer", Tag(42_ti)}}));
  const auto bytes = source.encode(std::nullopt);
  Options options;
  options.named = false;
  NbtParser decoded = NbtParser::parse(bytes, options);
  if (decoded.status() != Status::Complete) {
    return 1;
  }
  auto tag = decoded.readTag();
  const auto *answer = tag.find("answer");
  return answer != nullptr && std::get<Tag::Int>(answer->payload()) == 42 ? 0 : 1;
}
