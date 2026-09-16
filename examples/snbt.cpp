#include <iostream>
#include <sstream>

#include "nbt/nbt.h"

int main() {
  const auto root = nbt::parseSnbt("{answer:42}");
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  nbt::serialize(stream, root);
  stream.seekg(0);
  std::cout << nbt::toSnbt(nbt::parse(stream), true) << '\n';
}
