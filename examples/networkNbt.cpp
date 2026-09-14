#include <cassert>
#include <iostream>

#include "nbt/nbt.h"

int main() {
  using namespace nbt;

  // Network NBT: root must be a compound and has no name or root-name length.
  const Tag root = Tag::compound("", {Tag::string("message", "hello"), Tag::byte("online", 1)});
  const auto networkBytes = serialize(root, BinaryFormat::Network);

  std::cout << "Network NBT starts with 0x" << std::hex << std::to_integer<unsigned>(networkBytes.front()) << std::dec << '\n';
  std::cout << "Size: " << networkBytes.size() << " bytes\n";

  ParseOptions networkOptions;
  networkOptions.format = BinaryFormat::Network;
  const auto parsed = parse(networkBytes, networkOptions);
  std::cout << "Parsed network root: " << toSnbt(parsed, false) << '\n';

  // Split the packet into tiny chunks and parse again.
  Buffer chain;
  for (std::size_t i = 0; i < networkBytes.size(); ++i) {
    chain.append(std::span<const std::byte>{networkBytes.data() + i, 1});
  }
  const auto parsedFromChain = parse(chain, networkOptions);
  assert(equivalent(parsed, parsedFromChain));
  std::cout << "Parsed from one-byte chain successfully\n";

  return 0;
}
