#include <iostream>

#include "nbt/nbt.h"

int main() {
  nbt::Builder builder("root");
  builder.add(nbt::intTag("DataVersion", 3955)).add(nbt::stringTag("Name", "Example")).beginList("Values", nbt::Type::Int).add(nbt::intTag("", 1)).add(nbt::intTag("", 2)).add(nbt::intTag("", 3)).end();
  const auto root = builder.build();
  const auto binary = nbt::serialize(root);
  std::cout << "Serialized bytes: " << binary.size() << '\n' << nbt::toSnbt(root, true) << '\n';
}
