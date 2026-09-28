#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nbt/nbt.h"

#include "gtest/gtest.h"

namespace {

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

} // namespace

TEST(CoreTests, BufferPrimitives) {
  nbt::Buffer buffer;
  const std::array input{std::byte{1}, std::byte{2}, std::byte{3}};
  buffer.append(input.data(), input.data() + input.size());
  EXPECT_EQ(buffer.size(), input.size());
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(std::memcmp(buffer.data(), input.data(), input.size()), 0);

  const auto [storage, available] = buffer.preallocate(2);
  ASSERT_NE(storage, nullptr);
  ASSERT_GE(available, 2);
  const std::array appended{std::byte{4}, std::byte{1}};
  std::memcpy(storage, appended.data(), appended.size());
  buffer.postallocate(appended.size());
  EXPECT_EQ(buffer.size(), 5);
  EXPECT_EQ(std::memcmp(buffer.data() + input.size(), appended.data(), appended.size()), 0);

  buffer.reset();
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.capacity(), 0);
}

TEST(CoreTests, BufferGrowthAndOverlapRejection) {
  std::array<std::byte, 64> source{};
  for (std::size_t index = 0; index < source.size(); ++index) {
    source[index] = static_cast<std::byte>(index);
  }
  const auto extension = source;

  nbt::Buffer borrowed{std::span<const std::byte>{source}};
  EXPECT_EQ(borrowed.capacity(), 0);
  const auto *borrowedData = borrowed.data();
  borrowed.append(borrowed.begin(), borrowed.end());
  EXPECT_GT(borrowed.capacity(), 0);
  EXPECT_NE(borrowed.data(), borrowedData);
  EXPECT_EQ(borrowed.size(), source.size() * 2);
  EXPECT_EQ(std::memcmp(borrowed.data(), source.data(), source.size()), 0);
  EXPECT_EQ(std::memcmp(borrowed.data() + source.size(), source.data(), source.size()), 0);
  borrowed.append(extension.data(), extension.data() + extension.size());
  EXPECT_EQ(borrowed.size(), source.size() * 2 + extension.size());
  EXPECT_GE(borrowed.capacity(), borrowed.size());
  EXPECT_EQ(std::memcmp(borrowed.data(), source.data(), source.size()), 0);
  EXPECT_EQ(std::memcmp(borrowed.data() + source.size(), extension.data(), extension.size()), 0);

  const auto sizeBeforeOverlap = borrowed.size();
  const auto *overlapBegin = borrowed.data() + 1;
  EXPECT_THROW(borrowed.append(overlapBegin, overlapBegin + 8), std::invalid_argument);
  EXPECT_EQ(borrowed.size(), sizeBeforeOverlap);

  nbt::Buffer limited;
  const auto [storage, available] = limited.preallocate(4, 64, 3);
  EXPECT_EQ(storage, nullptr);
  EXPECT_EQ(available, 0);

  nbt::Buffer reserved{1};
  EXPECT_THROW(reserved.postallocate(reserved.capacity() + 1), std::overflow_error);

  EXPECT_EQ(nbt::BufferUtils::growthSize(0), 0);
  EXPECT_EQ(nbt::BufferUtils::growthSize(64), 64);
  EXPECT_EQ(nbt::BufferUtils::growthSize(65), 128);
  EXPECT_EQ(nbt::BufferUtils::growthSize((std::numeric_limits<std::size_t>::max)()), (std::numeric_limits<std::size_t>::max)());
}

TEST(CoreTests, TagNameOperatorAcceptsStringTypes) {
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

  EXPECT_EQ(stringTag.name, stringName);
  EXPECT_EQ(viewTag.name, viewName);
  EXPECT_EQ(pointerTag.name, pointerName);
  EXPECT_EQ(arrayTag.name, arrayName);
  EXPECT_EQ(&namedTag, &existingTag);
  EXPECT_EQ(namedTag.name, viewName);
}

TEST(CoreTests, TagValueOperatorAcceptsNbtValues) {
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

  EXPECT_EQ(directViewTag.name, "direct");
  EXPECT_EQ(directArrayTag.name, "direct-array");
  EXPECT_EQ(byteTag.type, nbt::Type::Byte);
  EXPECT_EQ(shortTag.type, nbt::Type::Short);
  EXPECT_EQ(intTag.type, nbt::Type::Int);
  EXPECT_EQ(longTag.type, nbt::Type::Long);
  EXPECT_EQ(floatTag.type, nbt::Type::Float);
  EXPECT_EQ(doubleTag.type, nbt::Type::Double);
  EXPECT_EQ(stringTag.type, nbt::Type::String);
  EXPECT_EQ(byteArrayTag.type, nbt::Type::ByteArray);
  EXPECT_EQ(intArrayTag.type, nbt::Type::IntArray);
  EXPECT_EQ(longArrayTag.type, nbt::Type::LongArray);
  EXPECT_EQ(containerTag.type, nbt::Type::Compound);
}

TEST(CoreTests, CanonicalBytes) {
  using N = nbt::Nbt;

  const auto bytes = N(nbt::Tag("answer", nbt::Tag::Int{42})).encode();
  const std::array expected{
      std::byte{static_cast<unsigned char>(nbt::Type::Int)}, std::byte{0}, std::byte{6}, std::byte{'a'}, std::byte{'n'}, std::byte{'s'}, std::byte{'w'}, std::byte{'e'}, std::byte{'r'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{42}};
  EXPECT_TRUE(equalBytes(bytes, expected));

  const auto networkBytes = N(nbt::Tag("", nbt::Tag::Container{})).encode(false);
  const std::array expectedNetwork{std::byte{static_cast<unsigned char>(nbt::Type::Compound)}, std::byte{0}};
  EXPECT_TRUE(equalBytes(networkBytes, expectedNetwork));
}

TEST(CoreTests, BorrowedLazyRead) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  const std::span<const std::byte> view(bytes);
  N document = N::parse(view);
  EXPECT_TRUE(document.valid());
  EXPECT_FALSE(document.ownsBytes());
  EXPECT_EQ(document.root().name(), "root");
  EXPECT_EQ(document.root().payloadBegin(), 0);
  EXPECT_EQ(document.root().payloadEnd(), bytes.size());
  EXPECT_EQ(document.root().find("answer").as<nbt::Type::Int>(), 42);
  EXPECT_EQ(document.root().find("name").as<nbt::Type::String>(), "Alex");
  EXPECT_EQ(document.root().find("values").child(1).as<nbt::Type::Int>(), 2);
  EXPECT_TRUE(equalBytes(document.encode(), bytes));
}

TEST(CoreTests, OwnedRead) {
  using N = nbt::Nbt;
  const auto source = N(sample()).encode();
  N document = N::parse(source);
  EXPECT_TRUE(document.valid());
  EXPECT_TRUE(document.ownsBytes());
  EXPECT_EQ(document.materialize().type, nbt::Type::Compound);
}

TEST(CoreTests, Continuation) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  const std::span<const std::byte> view(bytes);
  const auto firstSplit = bytes.size() / 3;
  const auto secondSplit = (bytes.size() * 2) / 3;

  const std::vector<std::byte> original(bytes.begin(), bytes.end());
  N borrowed = N::parseAtMost(view.first(firstSplit));
  EXPECT_EQ(borrowed.status(), nbt::Status::NeedMoreData);
  EXPECT_FALSE(borrowed.ownsBytes());
  borrowed.append(view.subspan(firstSplit, secondSplit - firstSplit));
  EXPECT_EQ(borrowed.status(), nbt::Status::NeedMoreData);
  EXPECT_TRUE(borrowed.ownsBytes());
  EXPECT_TRUE(equalBytes(bytes, original));
  borrowed.append(view.subspan(secondSplit));
  EXPECT_EQ(borrowed.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(borrowed.encode(), bytes));
  EXPECT_TRUE(equalBytes(bytes, original));

  const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + firstSplit);
  N owned = N::parseAtMost(truncated);
  EXPECT_EQ(owned.status(), nbt::Status::NeedMoreData);
  EXPECT_TRUE(owned.ownsBytes());
  EXPECT_TRUE(equalBytes(owned.bytes(), truncated));

  N appended;
  appended.append(view.first(firstSplit));
  EXPECT_EQ(appended.status(), nbt::Status::NeedMoreData);
  EXPECT_TRUE(appended.ownsBytes());
  appended.append(view.subspan(firstSplit, secondSplit - firstSplit));
  EXPECT_EQ(appended.status(), nbt::Status::NeedMoreData);
  appended.append(view.subspan(secondSplit));
  EXPECT_EQ(appended.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(appended.bytes(), bytes));
  EXPECT_TRUE(equalBytes(appended.encode(), bytes));
}

TEST(CoreTests, EmptyAppendIsNoOp) {
  using N = nbt::Nbt;
  const std::span<const std::byte> empty;

  N document;
  document.append(empty);
  EXPECT_EQ(document.status(), nbt::Status::Empty);
  EXPECT_FALSE(document.ownsBytes());

  const auto bytes = N(sample()).encode();
  document = N::parse(bytes);
  document.append(empty);
  EXPECT_EQ(document.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(document.bytes(), bytes));
}

TEST(CoreTests, ParserStateAndOptions) {
  using N = nbt::Nbt;
  using E = nbt::Exception;

  N empty = N::parseAtMost(std::span<const std::byte>{});
  EXPECT_EQ(empty.status(), nbt::Status::NeedMoreData);
  EXPECT_FALSE(empty.valid());
  EXPECT_THROW((void)empty.root(), std::logic_error);
  empty.clear();
  EXPECT_EQ(empty.status(), nbt::Status::Empty);

  const auto bytes = N(sample()).encode();
  std::vector<std::byte> trailing(bytes.begin(), bytes.end());
  trailing.push_back(std::byte{0x7f});
  auto withTrailing = N::parse(trailing);
  EXPECT_TRUE(withTrailing.valid());
  EXPECT_EQ(withTrailing.bytes().size(), trailing.size());
  EXPECT_TRUE(equalBytes(withTrailing.encode(), bytes));

  nbt::Options inputLimit;
  inputLimit.maxInputBytes = bytes.size() - 1;
  EXPECT_THROW((void)N::parse(bytes, inputLimit), E);

  nbt::Options depthLimit;
  depthLimit.maxDepth = 0;
  EXPECT_THROW((void)N::parse(bytes, depthLimit), E);

  nbt::Options elementLimit;
  elementLimit.maxContainerElements = 1;
  EXPECT_THROW((void)N::parse(bytes, elementLimit), E);

  nbt::Options nodeLimit;
  nodeLimit.maxTotalNodes = 1;
  EXPECT_THROW((void)N::parse(bytes, nodeLimit), E);
}

TEST(CoreTests, ScalarAndStringRoundTrips) {
  using N = nbt::Nbt;
  const std::string embedded{"a\0\xC3\xA9", 4};
  const auto root = nbt::Tag("root",
                             {nbt::Tag("byte", nbt::Tag::Byte{-2}),
                              nbt::Tag("short", nbt::Tag::Short{-300}),
                              nbt::Tag("int", nbt::Tag::Int{-70000}),
                              nbt::Tag("long", nbt::Tag::Long{-9000000000LL}),
                              nbt::Tag("float", nbt::Tag::Float{-1.25F}),
                              nbt::Tag("double", nbt::Tag::Double{2.5}),
                              nbt::Tag("text", embedded)});
  const auto bytes = N(root).encode();
  const auto document = N::parse(bytes);
  const auto view = document.root();
  EXPECT_EQ(view.find("byte").as<nbt::Type::Byte>(), -2);
  EXPECT_EQ(view.find("short").as<nbt::Type::Short>(), -300);
  EXPECT_EQ(view.find("int").as<nbt::Type::Int>(), -70000);
  EXPECT_EQ(view.find("long").as<nbt::Type::Long>(), -9000000000LL);
  EXPECT_EQ(view.find("float").as<nbt::Type::Float>(), -1.25F);
  EXPECT_EQ(view.find("double").as<nbt::Type::Double>(), 2.5);
  EXPECT_EQ(view.find("text").as<nbt::Type::String>(), embedded);
  EXPECT_EQ(view.find("text").as<nbt::Type::String>().size(), 4);
  EXPECT_TRUE(equalBytes(document.encode(), bytes));
}

TEST(CoreTests, NetworkFormat) {
  using N = nbt::Nbt;
  auto root = sample();
  root.name.clear();
  const auto bytes = N(root).encode(false);
  nbt::Options options;
  options.named = false;
  N document = N::parse(std::as_bytes(std::span(bytes)), options);
  EXPECT_EQ(document.status(), nbt::Status::Complete);
  EXPECT_EQ(document.root().type(), nbt::Type::Compound);
}

TEST(CoreTests, EveryTruncation) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    N document = N::parseAtMost(std::as_bytes(std::span(bytes).first(size)));
    EXPECT_EQ(document.status(), nbt::Status::NeedMoreData);
  }
}

TEST(CoreTests, ArrayViews) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document = N::parse(bytes);
  EXPECT_TRUE(document.valid());

  const auto scores = document.root().find("scores").as<nbt::Type::IntArray>();
  EXPECT_EQ(scores.size(), 3);
  EXPECT_EQ(scores[0], 10);
  EXPECT_EQ(scores[1], 20);
  EXPECT_EQ(scores[2], 30);
  std::int32_t scoreTotal = 0;
  for (const auto score : scores) {
    scoreTotal += score;
  }
  EXPECT_EQ(scoreTotal, 60);

  const auto big = document.root().find("big").as<nbt::Type::LongArray>();
  EXPECT_EQ(big.size(), 2);
  EXPECT_EQ(big.front(), 100);
  EXPECT_EQ(big.back(), 200);

  const auto scoresTag = document.root().find("scores").materialize();
  const auto &scoresVec = std::get<std::vector<nbt::Tag::Int>>(scoresTag.payload);
  EXPECT_EQ(scoresVec[2], 30);
  const auto materializedRoot = document.materialize();
  EXPECT_EQ(materializedRoot.name, "root");
  EXPECT_EQ(materializedRoot.type, nbt::Type::Compound);
}

TEST(CoreTests, ContainerViews) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document = N::parse(bytes);
  EXPECT_TRUE(document.valid());

  const auto values = document.root().find("values").as<nbt::Type::List>();
  EXPECT_EQ(values.size(), 2);
  EXPECT_EQ(values[0].as<nbt::Type::Int>(), 1);
  EXPECT_EQ(values[1].as<nbt::Type::Int>(), 2);

  const auto root = document.root().as<nbt::Type::Compound>();
  EXPECT_GE(root.size(), 4);
  const auto answer = root.find("answer");
  EXPECT_TRUE(static_cast<bool>(answer));
  EXPECT_EQ(answer.as<nbt::Type::Int>(), 42);
  EXPECT_FALSE(root.find("missing"));
  EXPECT_FALSE(root.child(root.size()));
  EXPECT_THROW((void)answer.as<nbt::Type::String>(), std::bad_variant_access);

  std::size_t count = 0;
  for (const auto child : root) {
    (void)child;
    ++count;
  }
  EXPECT_EQ(count, root.size());
}

TEST(CoreTests, Encode) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  N document = N::parse(bytes);

  nbt::Buffer buffer;
  document.encode(buffer);
  EXPECT_TRUE(equalBytes(buffer, bytes));
}

TEST(CoreTests, ArrayListsRoundTrip) {
  using N = nbt::Nbt;

  const auto root = nbt::Tag("root",
                             nbt::Tag::Container{nbt::Tag("bytes", nbt::Type::ByteArray, {nbt::Tag(nbt::Tag::ByteArray{1, -2})}),
                                                 nbt::Tag("ints", nbt::Type::IntArray, {nbt::Tag(nbt::Tag::IntArray{3, -4})}),
                                                 nbt::Tag("longs", nbt::Type::LongArray, {nbt::Tag(nbt::Tag::LongArray{5, -6})})});

  const auto document = N::parse(N(root).encode());
  EXPECT_EQ(document.root().find("bytes").child(0).as<nbt::Type::ByteArray>().size(), 2);
  EXPECT_EQ(document.root().find("ints").child(0).as<nbt::Type::IntArray>()[1], -4);
  EXPECT_EQ(document.root().find("longs").child(0).as<nbt::Type::LongArray>()[1], -6);
}

TEST(CoreTests, EndListsRequireZeroElements) {
  using N = nbt::Nbt;

  const auto emptyList = nbt::Tag("empty", nbt::Type::End, {});
  const auto bytes = N(emptyList).encode();
  const std::array expected{
      std::byte{static_cast<unsigned char>(nbt::Type::List)}, std::byte{0}, std::byte{5}, std::byte{'e'}, std::byte{'m'}, std::byte{'p'}, std::byte{'t'}, std::byte{'y'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_TRUE(equalBytes(bytes, expected));
  EXPECT_EQ(N::parse(bytes).root().size(), 0);

  const auto normalizedList = nbt::Tag("normalized", nbt::Type::End, {nbt::Tag{}});
  EXPECT_TRUE(std::get<nbt::Tag::Container>(normalizedList.payload).empty());

  auto invalidList = emptyList;
  invalidList.payload = nbt::Tag::Container{nbt::Tag{}};
  auto wrongElementType = nbt::Tag("values", nbt::Type::Int, {nbt::Tag(nbt::Tag::Int{1})});
  std::get<nbt::Tag::Container>(wrongElementType.payload)[0] = nbt::Tag(nbt::Tag::Long{1});
#ifdef NBT_STRICT_MODE
  // Strict mode encodes invalid input as-is; the malformed bytes fail on parse.
  EXPECT_THROW((void)N::parse(N(invalidList).encode()), nbt::Exception);
  const auto verbatim = N::parse(N(wrongElementType).encode());
  EXPECT_EQ(verbatim.root().type(), nbt::Type::List);
  EXPECT_EQ(verbatim.root().elementType(), nbt::Type::Int);
  EXPECT_EQ(verbatim.root().size(), 1);
#else
  EXPECT_TRUE(equalBytes(N(invalidList).encode(), bytes));

  const auto skipped = N::parse(N(wrongElementType).encode());
  EXPECT_EQ(skipped.root().type(), nbt::Type::List);
  EXPECT_EQ(skipped.root().elementType(), nbt::Type::End);
  EXPECT_EQ(skipped.root().size(), 0);
#endif

  auto namedElement = nbt::Tag("values", nbt::Type::Int, {nbt::Tag(nbt::Tag::Int{1})});
  std::get<nbt::Tag::Container>(namedElement.payload)[0].name = "ignored";
  const auto encoded = N::parse(N(namedElement).encode());
  EXPECT_EQ(encoded.root().size(), 1);
  EXPECT_EQ(encoded.root().child(0).as<nbt::Type::Int>(), 1);

  const auto compound = nbt::Tag("root", nbt::Tag::Container{nbt::Tag{}, nbt::Tag("value", nbt::Tag::Int{2})});
#ifdef NBT_STRICT_MODE
  EXPECT_THROW((void)N(compound).encode(), std::invalid_argument);
#else
  const auto pruned = N::parse(N(compound).encode());
  EXPECT_EQ(pruned.root().size(), 1);
  EXPECT_EQ(pruned.root().find("value").as<nbt::Type::Int>(), 2);
#endif
}

TEST(CoreTests, MalformedInput) {
  using N = nbt::Nbt;
  using E = nbt::Exception;

  const std::array invalidType{std::byte{0x7f}};
  EXPECT_THROW((void)N::parse(invalidType), E);

  const std::array namedEnd{std::byte{0}};
  EXPECT_THROW((void)N::parse(namedEnd), E);

  const std::array negativeArrayLength{std::byte{static_cast<unsigned char>(nbt::Type::ByteArray)}, std::byte{0}, std::byte{0}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  EXPECT_THROW((void)N::parse(negativeArrayLength), E);

  // Parsing named bytes as unnamed is not an error: any root type is accepted and
  // the caller is responsible for interpreting the result correctly.
  auto scalar = N(nbt::Tag("value", nbt::Tag::Int{1}));
  nbt::Options network;
  network.named = false;
  const auto misread = N::parse(scalar.encode(), network);
  EXPECT_TRUE(misread.valid());
  EXPECT_EQ(misread.root().type(), nbt::Type::Int);
}

TEST(CoreTests, NamedFlag) {
  using N = nbt::Nbt;

  const auto named = N(sample()).encode(true);
  const auto unnamed = N(sample()).encode(false);
  EXPECT_EQ(unnamed.size(), named.size() - 2 - 4); // minus u16 length and "root"
  EXPECT_TRUE(equalBytes(std::span(unnamed).subspan(1), std::span(named).subspan(7)));

  const auto fromNamed = N::parse(named);
  EXPECT_TRUE(equalBytes(fromNamed.encode(false), unnamed));
  EXPECT_TRUE(equalBytes(fromNamed.encode(true), named));

  nbt::Options options;
  options.named = false;
  const auto fromUnnamed = N::parse(unnamed, options);
  EXPECT_TRUE(fromUnnamed.valid());
  EXPECT_EQ(fromUnnamed.root().name(), "");
  const auto renamed = fromUnnamed.encode(true);
  EXPECT_EQ(renamed.size(), unnamed.size() + 2);
  EXPECT_TRUE(equalBytes(std::span(renamed).subspan(3), std::span(unnamed).subspan(1)));

  N incremental;
  incremental.append(std::span<const std::byte>(unnamed), false);
  EXPECT_EQ(incremental.status(), nbt::Status::Complete);
  EXPECT_EQ(incremental.root().type(), nbt::Type::Compound);
}

TEST(CoreTests, NonCompoundRoot) {
  using N = nbt::Nbt;

  const auto scalar = N(nbt::Tag("solo", nbt::Tag::Int{7})).encode();
  const auto document = N::parse(scalar);
  EXPECT_TRUE(document.valid());
  EXPECT_EQ(document.root().type(), nbt::Type::Int);
  EXPECT_EQ(document.root().as<nbt::Type::Int>(), 7);

  nbt::Options options;
  options.named = false;
  const auto unnamedScalar = N(nbt::Tag("solo", nbt::Tag::Int{7})).encode(false);
  const auto unnamedDocument = N::parse(unnamedScalar, options);
  EXPECT_TRUE(unnamedDocument.valid());
  EXPECT_EQ(unnamedDocument.root().as<nbt::Type::Int>(), 7);
}

TEST(CoreTests, TruncatedParseThrowsNeedMoreData) {
  using N = nbt::Nbt;
  const auto bytes = N(sample()).encode();
  EXPECT_THROW((void)N::parse(std::span<const std::byte>(bytes).first(bytes.size() - 1)), nbt::NeedMoreDataException);
  EXPECT_THROW((void)N::parse(std::span<const std::byte>{}), nbt::NeedMoreDataException);
}

TEST(CoreTests, EncodeErrors) {
  using N = nbt::Nbt;

  N empty;
  EXPECT_THROW((void)empty.encode(), nbt::Exception);

  N endRoot{nbt::Tag{}};
  EXPECT_THROW((void)endRoot.encode(), std::invalid_argument);

  const std::array incompleteBytes{std::byte{0x0A}};
  N incomplete = N::parseAtMost(std::span<const std::byte>{incompleteBytes});
  EXPECT_THROW((void)incomplete.encode(), nbt::Exception);
}

TEST(CoreTests, MaterializeAllTypes) {
  using N = nbt::Nbt;
  const auto tag = N::parse(N(sample()).encode()).materialize();
  EXPECT_EQ(tag.type, nbt::Type::Compound);
  const auto &children = std::get<nbt::Tag::Container>(tag.payload);
  EXPECT_EQ(std::get<nbt::Tag::Int>(children[0].payload), 42);
  EXPECT_EQ(std::get<nbt::Tag::String>(children[1].payload), "Alex");
  EXPECT_EQ(children[2].elementType, nbt::Type::Int);
  EXPECT_EQ(std::get<nbt::Tag::IntArray>(children[3].payload).size(), 3);
  EXPECT_EQ(std::get<nbt::Tag::LongArray>(children[4].payload)[1], 200);
}

#ifndef NBT_STRICT_MODE
TEST(CoreTests, LenientEncodingIgnoresInvalidValues) {
  using N = nbt::Nbt;

  nbt::Tag wrongPayload{"bad", std::string_view("x")};
  wrongPayload.type = nbt::Type::Int;

  nbt::Tag containerArray{"converted", nbt::Type::End, {}};
  containerArray.type = nbt::Type::IntArray;
  containerArray.payload = nbt::Tag::Container{
      nbt::Tag(std::int32_t{4}),
      nbt::Tag(std::int64_t{9}),
      nbt::Tag(std::int32_t{6}),
  };

  nbt::Tag root{"root", std::vector<nbt::Tag>{
      nbt::Tag("ok", std::int32_t{7}),
      nbt::Tag{},
      wrongPayload,
      containerArray,
      nbt::Tag("l", nbt::Type::Int, std::vector<nbt::Tag>{
          nbt::Tag(std::int32_t{1}),
          nbt::Tag(std::string_view("junk")),
      }),
  }};

  const auto document = N::parse(N(root).encode());
  const auto view = document.root();
  EXPECT_EQ(view.find("ok").as<nbt::Type::Int>(), 7);
  EXPECT_FALSE(view.find("bad"));
  const auto converted = view.find("converted").as<nbt::Type::IntArray>();
  ASSERT_EQ(converted.size(), 2);
  EXPECT_EQ(converted[0], 4);
  EXPECT_EQ(converted[1], 6);
  const auto list = view.find("l").as<nbt::Type::List>();
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].as<nbt::Type::Int>(), 1);
}
#else
TEST(CoreTests, StrictEncodingRejectsInvalidValues) {
  using N = nbt::Nbt;

  nbt::Tag wrongPayload{"bad", std::string_view("x")};
  wrongPayload.type = nbt::Type::Int;
  EXPECT_THROW((void)N(wrongPayload).encode(), std::bad_variant_access);

  nbt::Tag endChild{"root", std::vector<nbt::Tag>{nbt::Tag{}, nbt::Tag("ok", std::int32_t{7})}};
  EXPECT_THROW((void)N(endChild).encode(), std::invalid_argument);
}
#endif
