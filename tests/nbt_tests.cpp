#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
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
  const std::vector<std::byte> bytes = N(sample()).encode();
  const std::span<const std::byte> view(bytes);
  N document = N::parse(view);
  check(document.complete());
  check(!document.ownsBytes());
  check(document.root().name() == "root");
  check(document.root().begin() == 0);
  check(document.root().end() == bytes.size());
  check(document.root().find("answer").as<N::Type::Int>() == 42);
  check(document.root().find("name").as<N::Type::String>() == "Alex");
  check(document.root().find("values").child(1).as<N::Type::Int>() == 2);
  check(document.encode() == bytes);
}

void testOwnedRead() {
  using N = nbt::Nbt;
  const auto source = N(sample()).encode();
  N document = N::parse(source);
  check(document.complete());
  check(document.ownsBytes());
  check(document.materialize().type == N::Type::Compound);
}

void testContinuation() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  const std::span<const std::byte> view(bytes);
  N borrowed = N::parse(view.first(bytes.size() / 2));
  check(borrowed.status() == N::Status::NeedMoreData);
  borrowed.reset(view);
  check(borrowed.status() == N::Status::Complete);

  N append;
  append.append(view.first(view.size() / 2));
  check(append.status() == N::Status::NeedMoreData);
  append.append(view.subspan(view.size() / 2));
  check(append.status() == N::Status::Complete);
  check(append.ownsBytes());
}

void testNetworkFormat() {
  using N = nbt::Nbt;
  auto root = sample();
  root.name.clear();
  const auto bytes = N(root).encode(N::Format::Network);
  N::Options options;
  options.format = N::Format::Network;
  N document = N::parse(std::as_bytes(std::span(bytes)), options);
  check(document.status() == N::Status::Complete);
  check(document.root().type() == N::Type::Compound);
}

void testEveryTruncation() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    N document = N::parse(std::as_bytes(std::span(bytes).first(size)));
    check(document.status() == N::Status::NeedMoreData);
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
    N document = N::parse(invalid);
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
