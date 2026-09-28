#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  using namespace nbt::tag_literals;
  Nbt source(Tag("", std::vector<Tag>{Tag("answer", 42_ti)}));
  const auto bytes = source.encode(false);
  Options options;
  options.named = false;
  Nbt decoded = Nbt::parse(bytes, options);
  if (decoded.status() != Status::Complete) {
    return 1;
  }
  return decoded.root().find("answer").as<Type::Int>() == 42 ? 0 : 1;

  return 0;
}
