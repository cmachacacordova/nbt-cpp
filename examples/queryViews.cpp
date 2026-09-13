#include "nbt/nbt.hpp"

#include <iostream>

int main() {
  const auto binary = nbt::serialize(nbt::Tag::compound("root", {nbt::Tag::shortTag("Health", 20), nbt::Tag::string("Name", "Alex"), nbt::Tag::intArray("Position", {10, 64, -5})}));
  const auto indexed = nbt::tokenize(binary);
  const auto health = indexed.get<nbt::Type::Short>("Health");
  const auto name = indexed.get<nbt::Type::String>("Name");
  const auto position = indexed.get<nbt::Type::IntArray>("Position").value();
  std::cout << "Name: " << name.value() << '\n' << "Health: " << health.value() << '\n' << "Position: " << position[0] << ", " << position[1] << ", " << position[2] << '\n';
}
