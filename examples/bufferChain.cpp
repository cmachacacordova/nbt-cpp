#include <cassert>
#include <iostream>
#include <vector>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;

  const Tag root = Tag::compound("player", {Tag::string("name", "Alex"), Tag::shortTag("health", 20), Tag::intArray("pos", {10, 64, -5})});

  const auto bytes = serialize(root);
  std::cout << "Serialized " << bytes.size() << " bytes\n";

  // Build a non-contiguous Buffer from three spans.
  Buffer chain;
  chain.append(std::span<const std::byte>{bytes.data(), bytes.size() / 3})
      .append(std::span<const std::byte>{bytes.data() + bytes.size() / 3, bytes.size() / 3})
      .append(std::span<const std::byte>{bytes.data() + 2 * (bytes.size() / 3), bytes.size() - 2 * (bytes.size() / 3)});

  const auto parsed = parse(chain);
  std::cout << "Parsed from chain: " << toSnbt(parsed, false) << '\n';

  // Compress and then feed the compressed bytes as one-byte chunks.
  const auto packed = compress(bytes, Compression::Gzip);
  Buffer gzipChain;
  for (std::size_t i = 0; i < packed.size(); ++i) {
    gzipChain.append(std::span<const std::byte>{packed.data() + i, 1});
  }

  const auto unpacked = decompress(gzipChain, Compression::Auto);
  std::cout << "Decompressed " << unpacked.size() << " bytes from " << gzipChain.size() << " one-byte chunks\n";
  assert(unpacked == bytes);

  return 0;
}
