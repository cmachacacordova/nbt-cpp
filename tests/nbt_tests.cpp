#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nbt/nbt.h"

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

void testBufferPrimitives() {
  nbt::Buffer buffer;
  const std::array input{std::byte{1}, std::byte{2}, std::byte{3}};
  buffer.append(input.data(), input.data() + input.size());
  check(buffer.size() == input.size());
  check(!buffer.empty());
  check(std::memcmp(buffer.data(), input.data(), input.size()) == 0);

  const auto [storage, available] = buffer.preallocate(2);
  nbt::BufferWriter writer{static_cast<std::byte *>(storage), available};
  writer.put(std::byte{4});
  writer.write(std::span<const std::byte>{input}.first(1));
  check(writer.written() == 2);
  buffer.postallocate(writer.written());
  check(buffer.size() == 5);

  bool overflowRejected = false;
  std::array<std::byte, 1> oneByte{};
  try {
    nbt::BufferWriter limited{oneByte.data(), oneByte.size()};
    limited.write(input.data(), input.size());
  } catch (const std::overflow_error &) {
    overflowRejected = true;
  }
  check(overflowRejected);

  buffer.reset();
  check(buffer.empty());
  check(buffer.capacity() == 0);
}

void testTagNameOperatorAcceptsStringTypes() {
  using namespace nbt::tag_literals;

  const std::string stringName = "std::string";
  const std::string_view viewName = "std::string_view";
  const char *pointerName = "const char pointer";
  const char arrayName[] = "char array";

  const auto stringTag = stringName | nbt::Tag(1_ti);
  const auto viewTag = viewName | nbt::Tag(2_ti);
  const auto pointerTag = pointerName | nbt::Tag(3_ti);
  const auto arrayTag = arrayName | nbt::Tag(4_ti);
  auto existingTag = nbt::Tag(5_ti);
  auto &namedTag = viewName | existingTag;

  check(stringTag.name == stringName);
  check(viewTag.name == viewName);
  check(pointerTag.name == pointerName);
  check(arrayTag.name == arrayName);
  check(&namedTag == &existingTag);
  check(namedTag.name == viewName);
}

void testTagValueOperatorAcceptsNbtValues() {
  using namespace nbt::tag_literals;

  const std::string name = "value";
  const nbt::Tag::Byte byteValue = 1;
  const nbt::Tag::Short shortValue = 2;
  const nbt::Tag::Int intValue = 3;
  const nbt::Tag::Long longValue = 4;
  const nbt::Tag::Float floatValue = 5.0F;
  const nbt::Tag::Double doubleValue = 6.0;
  const nbt::Tag::String stringValue = "text";
  const nbt::Tag::ByteArray byteArrayValue{1, 2};
  const nbt::Tag::IntArray intArrayValue{3, 4};
  const nbt::Tag::LongArray longArrayValue{5, 6};
  const nbt::Tag::Container containerValue{nbt::Tag("child", 7_ti)};
  const auto directViewTag = nbt::Tag(std::string_view{"direct"}, intValue);
  const auto directArrayTag = nbt::Tag("direct-array", intArrayValue);

  const auto byteTag = name | byteValue;
  const auto shortTag = name | shortValue;
  const auto intTag = name | intValue;
  const auto longTag = name | longValue;
  const auto floatTag = name | floatValue;
  const auto doubleTag = name | doubleValue;
  const auto stringTag = name | stringValue;
  const auto byteArrayTag = name | byteArrayValue;
  const auto intArrayTag = name | intArrayValue;
  const auto longArrayTag = name | longArrayValue;
  const auto containerTag = name | containerValue;

  check(directViewTag.name == "direct");
  check(directArrayTag.name == "direct-array");
  check(byteTag.type == nbt::Type::Byte);
  check(shortTag.type == nbt::Type::Short);
  check(intTag.type == nbt::Type::Int);
  check(longTag.type == nbt::Type::Long);
  check(floatTag.type == nbt::Type::Float);
  check(doubleTag.type == nbt::Type::Double);
  check(stringTag.type == nbt::Type::String);
  check(byteArrayTag.type == nbt::Type::ByteArray);
  check(intArrayTag.type == nbt::Type::IntArray);
  check(longArrayTag.type == nbt::Type::LongArray);
  check(containerTag.type == nbt::Type::Compound);
}

void testCanonicalBytes() {
  using N = nbt::Nbt;

  const auto bytes = N(nbt::Tag("answer", nbt::Tag::Int{42})).encode();
  const std::array expected{
      std::byte{static_cast<unsigned char>(nbt::Type::Int)}, std::byte{0}, std::byte{6}, std::byte{'a'}, std::byte{'n'}, std::byte{'s'}, std::byte{'w'}, std::byte{'e'}, std::byte{'r'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{42}};
  check(equalBytes(bytes, expected));

  const auto networkBytes = N(nbt::Tag("", nbt::Tag::Container{})).encode(nbt::Source::Network);
  const std::array expectedNetwork{std::byte{static_cast<unsigned char>(nbt::Type::Compound)}, std::byte{0}};
  check(equalBytes(networkBytes, expectedNetwork));
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

void testParserStateAndOptions() {
  using N = nbt::Nbt;

  N empty = N::parse(std::span<const std::byte>{});
  check(empty.status() == nbt::Status::NeedMoreData);
  check(!empty.complete());
  bool incompleteRootRejected = false;
  try {
    (void)empty.root();
  } catch (const std::logic_error &) {
    incompleteRootRejected = true;
  }
  check(incompleteRootRejected);
  empty.clear();
  check(empty.status() == nbt::Status::Empty);

  const auto bytes = N(sample()).encode();
  std::vector<std::byte> trailing(bytes.begin(), bytes.end());
  trailing.push_back(std::byte{0x7f});
  bool trailingRejected = false;
  try {
    (void)N::parse(trailing);
  } catch (const nbt::Error &error) {
    trailingRejected = error.offset() == bytes.size();
  }
  check(trailingRejected);

  nbt::Options allowTrailing;
  allowTrailing.requireCompleteInput = false;
  auto withTrailing = N::parse(trailing, allowTrailing);
  check(withTrailing.complete());
  check(withTrailing.bytes().size() == trailing.size());
  check(equalBytes(withTrailing.encode(), bytes));

  nbt::Options inputLimit;
  inputLimit.maxInputBytes = bytes.size() - 1;
  bool inputLimitRejected = false;
  try {
    (void)N::parse(bytes, inputLimit);
  } catch (const nbt::Error &) {
    inputLimitRejected = true;
  }
  check(inputLimitRejected);

  nbt::Options depthLimit;
  depthLimit.maxDepth = 0;
  bool depthLimitRejected = false;
  try {
    (void)N::parse(bytes, depthLimit);
  } catch (const nbt::Error &) {
    depthLimitRejected = true;
  }
  check(depthLimitRejected);

  nbt::Options elementLimit;
  elementLimit.maxContainerElements = 1;
  bool elementLimitRejected = false;
  try {
    (void)N::parse(bytes, elementLimit);
  } catch (const nbt::Error &) {
    elementLimitRejected = true;
  }
  check(elementLimitRejected);

  nbt::Options nodeLimit;
  nodeLimit.maxTotalNodes = 1;
  bool nodeLimitRejected = false;
  try {
    (void)N::parse(bytes, nodeLimit);
  } catch (const nbt::Error &) {
    nodeLimitRejected = true;
  }
  check(nodeLimitRejected);
}

void testScalarAndStringRoundTrips() {
  using N = nbt::Nbt;
  const std::string embedded{"a\0\xC3\xA9", 4};
  const auto root = nbt::Tag("root",
                             nbt::Tag::Container{nbt::Tag("byte", nbt::Tag::Byte{-2}),
                                                 nbt::Tag("short", nbt::Tag::Short{-300}),
                                                 nbt::Tag("int", nbt::Tag::Int{-70000}),
                                                 nbt::Tag("long", nbt::Tag::Long{-9000000000LL}),
                                                 nbt::Tag("float", nbt::Tag::Float{-1.25F}),
                                                 nbt::Tag("double", nbt::Tag::Double{2.5}),
                                                 nbt::Tag("text", embedded)});
  const auto bytes = N(root).encode();
  const auto document = N::parse(bytes);
  const auto view = document.root();
  check(view.find("byte").as<nbt::Type::Byte>() == -2);
  check(view.find("short").as<nbt::Type::Short>() == -300);
  check(view.find("int").as<nbt::Type::Int>() == -70000);
  check(view.find("long").as<nbt::Type::Long>() == -9000000000LL);
  check(view.find("float").as<nbt::Type::Float>() == -1.25F);
  check(view.find("double").as<nbt::Type::Double>() == 2.5);
  check(view.find("text").as<nbt::Type::String>() == embedded);
  check(view.find("text").as<nbt::Type::String>().size() == 4);
  check(equalBytes(document.encode(), bytes));
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
  std::int32_t scoreTotal = 0;
  for (const auto score : scores) {
    scoreTotal += score;
  }
  check(scoreTotal == 60);

  const auto big = document.root().find("big").as<nbt::Type::LongArray>();
  check(big.size() == 2);
  check(big.front() == 100);
  check(big.back() == 200);

  const auto scoresTag = document.root().find("scores").materialize();
  const auto &scoresVec = std::get<std::vector<nbt::Tag::Int>>(scoresTag.payload);
  check(scoresVec[2] == 30);
  const auto materializedRoot = document.materialize();
  check(materializedRoot.name == "root");
  check(materializedRoot.type == nbt::Type::Compound);
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
  check(!root.find("missing"));
  check(!root.child(root.size()));
  bool wrongTypeRejected = false;
  try {
    (void)answer.as<nbt::Type::String>();
  } catch (const std::bad_variant_access &) {
    wrongTypeRejected = true;
  }
  check(wrongTypeRejected);

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

  const std::array invalidType{std::byte{0x7f}};
  bool invalidTypeRejected = false;
  try {
    (void)N::parse(invalidType);
  } catch (const E &error) {
    invalidTypeRejected = error.offset() == 0;
  }
  check(invalidTypeRejected);

  const std::array namedEnd{std::byte{0}};
  bool namedEndRejected = false;
  try {
    (void)N::parse(namedEnd);
  } catch (const E &error) {
    namedEndRejected = error.offset() == 0;
  }
  check(namedEndRejected);

  const std::array negativeArrayLength{std::byte{static_cast<unsigned char>(nbt::Type::ByteArray)}, std::byte{0}, std::byte{0}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  bool negativeLengthRejected = false;
  try {
    (void)N::parse(negativeArrayLength);
  } catch (const E &error) {
    negativeLengthRejected = error.offset() == 3;
  }
  check(negativeLengthRejected);

  auto scalar = N(nbt::Tag("value", nbt::Tag::Int{1}));
  nbt::Options network;
  network.format = nbt::Source::Network;
  bool networkRootRejected = false;
  try {
    (void)N::parse(scalar.encode(), network);
  } catch (const E &error) {
    networkRootRejected = error.offset() == 0;
  }
  check(networkRootRejected);
}

} // namespace

int main() {
  try {
    testBufferPrimitives();
    testTagNameOperatorAcceptsStringTypes();
    testTagValueOperatorAcceptsNbtValues();
    testCanonicalBytes();
    testBorrowedLazyRead();
    testOwnedRead();
    testContinuation();
    testParserStateAndOptions();
    testScalarAndStringRoundTrips();
    testNetworkFormat();
    testEveryTruncation();
    testArrayViews();
    testContainerViews();
    testEncode();
    testMalformedInput();
  } catch (...) {
    return 1;
  }
  return 0;
}
