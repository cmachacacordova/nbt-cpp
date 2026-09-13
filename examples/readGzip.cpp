#include <filesystem>
#include <iostream>

#include "nbt/nbt.hpp"

int main(int argc, char **argv) {
  try {
    const std::filesystem::path path = argc > 1 ? argv[1] : NBT_CPP_EXAMPLE_LEVEL_PATH;
    const auto root = nbt::load(path, nbt::Compression::Gzip);
    std::cout << "File: " << path << '\n' << "Tags: " << nbt::size(root) << '\n' << nbt::toSnbt(root, true) << '\n';
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
