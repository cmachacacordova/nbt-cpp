#include <bit>
#include <cstdlib>

int main() {
  if constexpr (std::endian::native == std::endian::big) {
    return EXIT_SUCCESS; // Host byte order matches NBT (big-endian).
  }
  return EXIT_FAILURE; // Host requires byte swapping for NBT.
}
