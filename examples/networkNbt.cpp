#include <cassert>
#include <sstream>

#include "nbt/nbt.h"

int main() {
  const auto root = nbt::compoundTag("", {nbt::intTag("answer", 42)});
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  nbt::serialize(stream, root, nbt::BinaryFormat::Network);
  stream.seekg(0);
  nbt::ParseOptions options;
  options.format = nbt::BinaryFormat::Network;
  assert(nbt::equivalent(root, nbt::parse(stream, options)));
}
