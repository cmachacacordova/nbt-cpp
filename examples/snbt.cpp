#include <iostream>

#include "nbt/nbt.h"

int main() {
  try {
    const auto root = nbt::parseSnbt(R"({name:"Steve",health:20s,enabled:true,position:[1.5d,64.0d,-3.0d]})");
    const auto binary = nbt::serialize(root);
    const auto restored = nbt::parse(binary);
    std::cout << "Binary bytes: " << binary.size() << '\n' << nbt::toSnbt(restored, true) << '\n';
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
