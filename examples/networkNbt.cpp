#include "nbt/nbt.h"

int main() {
  using namespace nbt;
  Nbt source(Tag::compound("", {Tag::int32("answer", 42)}));
  const auto bytes = source.encode(Format::Network);
  Options options;
  options.format = Format::Network;
  Nbt decoded = Nbt::parse(bytes, options);
  if (decoded.status() != Status::Complete) {
    return 1;
  }
  return decoded.root().find("answer").as<Type::Int>() == 42 ? 0 : 1;
}
