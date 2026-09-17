#include <filesystem>

#include "nbt/nbt.h"
#include "nbt/utilities.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    return 0;
  }
  const auto document = nbt::NbtUtilities::load(std::filesystem::path(argv[1]), nbt::NbtUtilities::Compression::Gzip);
  return document.complete() ? 0 : 1;
}
