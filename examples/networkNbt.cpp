#include "nbt/nbt.h"

int main() {
  using Nbt = nbt::Nbt;
  Nbt source(Nbt::compound("", {Nbt::int32("answer", 42)}));
  const auto bytes = source.encode(Nbt::Format::Network);
  Nbt::Options options;
  options.format = Nbt::Format::Network;
  Nbt decoded = Nbt::parse(bytes, options);
  if (decoded.status() != Nbt::Status::Complete) {
    return 1;
  }
  return decoded.root().find("answer").as<Nbt::Type::Int>() == 42 ? 0 : 1;
}
