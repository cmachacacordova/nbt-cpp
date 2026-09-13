#include "nbt/nbt.hpp"

#include <iostream>

int main() {
  nbt::Builder builder("root");
  builder.add(nbt::Tag::intTag("DataVersion", 3955)).add(nbt::Tag::string("Name", "Example")).beginList("Values", nbt::Type::Int).add(nbt::Tag::intTag("", 1)).add(nbt::Tag::intTag("", 2)).add(nbt::Tag::intTag("", 3)).end();
  const auto root = builder.build();
  const auto binary = nbt::serialize(root);
  std::cout << "Serialized bytes: " << binary.size() << '\n' << nbt::toSnbt(root, true) << '\n';
}
