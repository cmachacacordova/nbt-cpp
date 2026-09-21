#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

bool equalBytes(const auto &left, const auto &right) {
  return left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size()) == 0;
}

nbt::Tag sample() {
  using namespace std::string_literals;
  return nbt::Tag("root",
                  std::vector<nbt::Tag>{nbt::Tag("answer", int32_t(42)),
                                        nbt::Tag("name", "Alex"s),
                                        nbt::Tag("values", nbt::Type::Int, {nbt::Tag("", int32_t(1)), nbt::Tag("", int32_t(2))}),
                                        nbt::Tag("scores", std::vector<nbt::Tag::Int>{10, 20, 30}),
                                        nbt::Tag("big", std::vector<nbt::Tag::Long>{std::int64_t{100}, std::int64_t{200}}),
                                        nbt::Tag("nested", std::vector<nbt::Tag>{nbt::Tag("value", float(1.5))})});
}

void testBorrowedLazyRead() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  const std::span<const std::byte> view(bytes);
  N document = N::parse(view);
  check(document.complete());
  check(!document.ownsBytes());
  check(document.root().name() == "root");
  check(document.root().payloadBegin() == 0);
  check(document.root().payloadEnd() == bytes.size());
  check(document.root().find("answer").as<nbt::Type::Int>() == 42);
  check(document.root().find("name").as<nbt::Type::String>() == "Alex");
  check(document.root().find("values").child(1).as<nbt::Type::Int>() == 2);
  check(equalBytes(document.encode(), bytes));
}

void testOwnedRead() {
  using N = nbt::Nbt;
  const auto source = N(sample()).encode();
  N document = N::parse(source);
  check(document.complete());
  check(document.ownsBytes());
  check(document.materialize().type == nbt::Type::Compound);
}

void testContinuation() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  const std::span<const std::byte> view(bytes);
  N borrowed = N::parse(view.first(bytes.size() / 2));
  check(borrowed.status() == nbt::Status::NeedMoreData);
  borrowed = N::parse(view);
  check(borrowed.status() == nbt::Status::Complete);

  N append;
  append.append(view.first(view.size() / 2));
  check(append.status() == nbt::Status::NeedMoreData);
  append.append(view.subspan(view.size() / 2));
  check(append.status() == nbt::Status::Complete);
  check(append.ownsBytes());
}

void testNetworkFormat() {
  using N = nbt::Nbt;
  auto root = sample();
  root.name.clear();
  const auto bytes = N(root).encode(nbt::Source::Network);
  nbt::Options options;
  options.format = nbt::Source::Network;
  N document = N::parse(std::as_bytes(std::span(bytes)), options);
  check(document.status() == nbt::Status::Complete);
  check(document.root().type() == nbt::Type::Compound);
}

void testEveryTruncation() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    N document = N::parse(std::as_bytes(std::span(bytes).first(size)));
    check(document.status() == nbt::Status::NeedMoreData);
  }
}

#ifdef NBT_CPP_TEST_UTILITIES
void testUtilities() {
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;
  const auto snbt = U::toSnbt(sample());
  auto decoded = U::parseSnbt(snbt);
  decoded.name = "root";
  check(equalBytes(N(decoded).encode(), N(sample()).encode()));

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-lazy-test.dat";
  N document(sample());
  for (const auto compression : {U::Compression::None, U::Compression::Gzip, U::Compression::Zlib}) {
    U::save(path, document, compression);
    auto loaded = U::load(path);
    check(loaded.root().find("answer").as<nbt::Type::Int>() == 42);
  }
  std::filesystem::remove(path);
}
#endif

void testArrayViews() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document = N::parse(bytes);
  check(document.complete());

  const auto scores = document.root().find("scores").as<nbt::Type::IntArray>();
  check(scores.size() == 3);
  check(scores[0] == 10);
  check(scores[1] == 20);
  check(scores[2] == 30);

  const auto big = document.root().find("big").as<nbt::Type::LongArray>();
  check(big.size() == 2);
  check(big.front() == 100);
  check(big.back() == 200);

  const auto scoresTag = document.root().find("scores").materialize();
  const auto &scoresVec = std::get<std::vector<nbt::Tag::Int>>(scoresTag.payload);
  check(scoresVec[2] == 30);
}

void testContainerViews() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document = N::parse(bytes);
  check(document.complete());

  const auto values = document.root().find("values").as<nbt::Type::List>();
  check(values.size() == 2);
  check(values[0].as<nbt::Type::Int>() == 1);
  check(values[1].as<nbt::Type::Int>() == 2);

  const auto root = document.root().as<nbt::Type::Compound>();
  check(root.size() >= 4);
  const auto answer = root.find("answer");
  check(static_cast<bool>(answer));
  check(answer.as<nbt::Type::Int>() == 42);

  std::size_t count = 0;
  for (const auto child : root) {
    (void)child;
    ++count;
  }
  check(count == root.size());
}

void testEncode() {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document = N::parse(bytes);

  nbt::Buffer buffer;
  document.encode(buffer);
  check(equalBytes(buffer, bytes));
}

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
    testArrayViews();
    testContainerViews();
    testEncode();
#ifdef NBT_CPP_TEST_UTILITIES
    testUtilities();
#endif
    testMalformedInput();
  } catch (...) {
    return 1;
  }
  return 0;
}
