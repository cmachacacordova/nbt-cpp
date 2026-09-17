#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "nbt/nbt.h"
#ifdef NBT_CPP_TEST_UTILITIES
#include "nbt/utilities.h"
#endif

namespace {

void check(bool condition) {
  if (!condition) {
    throw std::runtime_error("test assertion failed");
  }
}

nbt::Nbt::Value sample() {
  using N = nbt::Nbt;
  return N::compound("root", {N::int32("answer", 42), N::string("name", "Alex"), N::list("values", N::Type::Int, {N::int32("", 1), N::int32("", 2)}), N::compound("nested", {N::float64("value", 1.5)})});
}

void testBorrowedLazyRead() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document;
  check(document.borrow(bytes) == N::Status::Complete);
  check(!document.ownsBytes());
  check(document.root().name() == "root");
  check(document.root().beginOffset() == 0);
  check(document.root().endOffset() == bytes.size());
  check(document.root().find("answer").as<N::Type::Int>() == 42);
  check(document.root().find("name").as<N::Type::String>() == "Alex");
  check(document.root().find("values").child(1).as<N::Type::Int>() == 2);
  check(document.encode() == bytes);
}

void testOwnedRead() {
  using N = nbt::Nbt;
  const auto source = N(sample()).encode();
  auto owned = std::make_unique<std::byte[]>(source.size());
  std::copy(source.begin(), source.end(), owned.get());
  N document;
  check(document.take(std::move(owned), source.size()) == N::Status::Complete);
  check(document.ownsBytes());
  check(document.materialize().type == N::Type::Compound);
}

void testContinuation() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N borrowed;
  check(borrowed.borrow(std::span(bytes).first(bytes.size() / 2)) == N::Status::NeedMoreData);
  check(borrowed.replaceBorrowed(bytes) == N::Status::Complete);

  N fed;
  check(fed.feed(std::span(bytes).first(bytes.size() / 2)) == N::Status::NeedMoreData);
  check(fed.feed(std::span(bytes).subspan(bytes.size() / 2)) == N::Status::Complete);
  check(fed.ownsBytes());
}

void testNetworkFormat() {
  using N = nbt::Nbt;
  auto root = sample();
  root.name.clear();
  const auto bytes = N(root).encode(N::Format::Network);
  N::Options options;
  options.format = N::Format::Network;
  N document;
  check(document.borrow(bytes, options) == N::Status::Complete);
  check(document.root().type() == N::Type::Compound);
}

void testEveryTruncation() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    N document;
    check(document.borrow(std::span(bytes).first(size)) == N::Status::NeedMoreData);
  }
}

#ifdef NBT_CPP_TEST_UTILITIES
void testUtilities() {
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;
  const auto snbt = U::toSnbt(sample());
  auto decoded = U::parseSnbt(snbt);
  decoded.name = "root";
  check(N(decoded).encode() == N(sample()).encode());

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-lazy-test.dat";
  N document(sample());
  for (const auto compression : {U::Compression::None, U::Compression::Gzip, U::Compression::Zlib}) {
    U::save(path, document, compression);
    auto loaded = U::load(path);
    check(loaded.root().find("answer").as<N::Type::Int>() == 42);
  }
  std::filesystem::remove(path);
}
#endif

void testMalformedInput() {
  using N = nbt::Nbt;
  using E = nbt::Error;
  const std::array invalid{std::byte{0x7f}};
  bool rejected = false;
  try {
    N document;
    (void)document.borrow(invalid);
  } catch (const E &) {
    rejected = true;
  }
  check(rejected);
}

} // namespace

int main() {
  try {
    testBorrowedLazyRead();
    testOwnedRead();
    testContinuation();
    testNetworkFormat();
    testEveryTruncation();
#ifdef NBT_CPP_TEST_UTILITIES
    testUtilities();
#endif
    testMalformedInput();
  } catch (...) {
    return 1;
  }
  return 0;
}
