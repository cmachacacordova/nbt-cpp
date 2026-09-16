#include <cassert>
#include <sstream>

#include "nbt/nbt.h"

int main() {
  const auto root = nbt::compoundTag("player", {nbt::intTag("health", 20)});
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  nbt::serialize(stream, root);
  stream.seekg(0);
  const auto tokens = nbt::tokenize(stream);
  assert(!tokens.tokens.empty());
}
