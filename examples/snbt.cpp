#include "nbt/nbt.h"
#include "nbt/utilities.h"

int main() {
  const auto root = nbt::NbtUtilities::parseSnbt("{answer:42}");
  return nbt::NbtUtilities::toSnbt(root) == "{\"answer\":42}" ? 0 : 1;
}
