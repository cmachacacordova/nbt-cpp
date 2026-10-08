#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "nbt/nbt.h"

#include "gtest/gtest.h"

namespace {

bool equalBytes(const auto &left, const auto &right) {
  return left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size()) == 0;
}

nbt::Tag sample() {
  using namespace std::string_literals;
  const nbt::Tag values(std::vector<nbt::Tag::Short>{1, 2});
  return nbt::Tag(nbt::Tag::Compound{{"answer"s, nbt::Tag(nbt::Tag::Int{42})},
                                     {"name"s, nbt::Tag("Alex"s)},
                                     {"values"s, values},
                                     {"scores"s, nbt::Tag(std::vector<nbt::Tag::Int>{10, 20, 30})},
                                     {"big"s, nbt::Tag(std::vector<nbt::Tag::Long>{std::int64_t{100}, std::int64_t{200}})},
                                     {"nested"s, nbt::Tag(nbt::Tag::Compound{{"value"s, nbt::Tag(1.5f)}})}});
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
  const auto &namedPair = viewName | existingTag;

  EXPECT_EQ(stringTag.first, stringName);
  EXPECT_EQ(viewTag.first, viewName);
  EXPECT_EQ(pointerTag.first, pointerName);
  EXPECT_EQ(arrayTag.first, arrayName);
  EXPECT_EQ(namedPair.first, viewName);
  EXPECT_EQ(namedPair.second.type(), nbt::Type::Int);
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
  const nbt::Tag::Compound compoundValue{{"child", nbt::Tag(nbt::Tag::Int{7})}};

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
  const auto compoundTag = name | compoundValue;

  EXPECT_EQ(byteTag.second.type(), nbt::Type::Byte);
  EXPECT_EQ(shortTag.second.type(), nbt::Type::Short);
  EXPECT_EQ(intTag.second.type(), nbt::Type::Int);
  EXPECT_EQ(longTag.second.type(), nbt::Type::Long);
  EXPECT_EQ(floatTag.second.type(), nbt::Type::Float);
  EXPECT_EQ(doubleTag.second.type(), nbt::Type::Double);
  EXPECT_EQ(stringTag.second.type(), nbt::Type::String);
  EXPECT_EQ(byteArrayTag.second.type(), nbt::Type::ByteArray);
  EXPECT_EQ(intArrayTag.second.type(), nbt::Type::IntArray);
  EXPECT_EQ(longArrayTag.second.type(), nbt::Type::LongArray);
  EXPECT_EQ(compoundTag.second.type(), nbt::Type::Compound);
}

TEST(CoreTests, CanonicalBytes) {
  using N = nbt::NbtParser;

  const auto bytes = N(nbt::Tag(nbt::Tag::Int{42})).encode("answer");
  const std::array expected{
      std::byte{static_cast<unsigned char>(nbt::Type::Int)}, std::byte{0}, std::byte{6}, std::byte{'a'}, std::byte{'n'}, std::byte{'s'}, std::byte{'w'}, std::byte{'e'}, std::byte{'r'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{42}};
  EXPECT_TRUE(equalBytes(bytes, expected));

  const auto networkBytes = N(nbt::Tag(nbt::Tag::Compound{})).encode(std::nullopt);
  const std::array expectedNetwork{std::byte{static_cast<unsigned char>(nbt::Type::Compound)}, std::byte{0}};
  EXPECT_TRUE(equalBytes(networkBytes, expectedNetwork));
}

TEST(CoreTests, BorrowedRead) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  const std::span<const std::byte> view(bytes);
  N document = N::parse(view);
  EXPECT_TRUE(document.valid());
  EXPECT_FALSE(document.ownsBytes());
  const auto rootView = document.root();
  EXPECT_EQ(rootView.name(), "root");
  auto tag = document.readTag();
  EXPECT_EQ(std::get<nbt::Tag::Int>(tag.find("answer")->payload()), 42);
  EXPECT_EQ(std::get<nbt::Tag::String>(tag.find("name")->payload()), "Alex");
  const auto &values = std::get<nbt::Tag::Array<nbt::Tag::Short>>(std::get<nbt::Tag::List>(tag.find("values")->payload()));
  EXPECT_EQ(values[0], 1);
  EXPECT_TRUE(equalBytes(document.encode("root"), bytes));
}

TEST(CoreTests, OwnedRead) {
  using N = nbt::NbtParser;
  const auto source = N(sample()).encode("root");
  N document = N::parse(source);
  EXPECT_TRUE(document.valid());
  EXPECT_TRUE(document.ownsBytes());
  EXPECT_EQ(document.readTag().type(), nbt::Type::Compound);
}

TEST(CoreTests, Continuation) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
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
  EXPECT_TRUE(equalBytes(borrowed.encode("root"), bytes));
  EXPECT_TRUE(equalBytes(bytes, original));

  const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + firstSplit);
  N owned = N::parseAtMost(truncated);
  EXPECT_EQ(owned.status(), nbt::Status::NeedMoreData);
  EXPECT_TRUE(owned.ownsBytes());
  EXPECT_TRUE(equalBytes(owned.bytes(), truncated));

  const auto unnamed = N(sample()).encode(std::nullopt);
  const std::span<const std::byte> unnamedView(unnamed);
  const auto unnamedFirstSplit = unnamed.size() / 3;
  const auto unnamedSecondSplit = (unnamed.size() * 2) / 3;
  N appended;
  appended.append(unnamedView.first(unnamedFirstSplit));
  EXPECT_EQ(appended.status(), nbt::Status::NeedMoreData);
  EXPECT_TRUE(appended.ownsBytes());
  appended.append(unnamedView.subspan(unnamedFirstSplit, unnamedSecondSplit - unnamedFirstSplit));
  EXPECT_EQ(appended.status(), nbt::Status::NeedMoreData);
  appended.append(unnamedView.subspan(unnamedSecondSplit));
  EXPECT_EQ(appended.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(appended.bytes(), unnamed));
  EXPECT_TRUE(equalBytes(appended.encode(std::nullopt), unnamed));
}

TEST(CoreTests, EmptyAppendIsNoOp) {
  using N = nbt::NbtParser;
  const std::span<const std::byte> empty;

  N document;
  document.append(empty);
  EXPECT_EQ(document.status(), nbt::Status::Empty);
  EXPECT_FALSE(document.ownsBytes());

  const auto bytes = N(sample()).encode("root");
  document = N::parse(bytes);
  document.append(empty);
  EXPECT_EQ(document.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(document.bytes(), bytes));
}

TEST(CoreTests, ParserStateAndOptions) {
  using N = nbt::NbtParser;
  using E = nbt::Exception;

  N empty = N::parseAtMost(std::span<const std::byte>{});
  EXPECT_EQ(empty.status(), nbt::Status::NeedMoreData);
  EXPECT_FALSE(empty.valid());
  EXPECT_THROW((void)empty.root(), std::logic_error);
  EXPECT_THROW((void)empty.readTag(), std::logic_error);
  empty.clear();
  EXPECT_EQ(empty.status(), nbt::Status::Empty);

  const auto bytes = N(sample()).encode("root");
  std::vector<std::byte> trailing(bytes.begin(), bytes.end());
  trailing.push_back(std::byte{0x7f});
  auto withTrailing = N::parse(trailing);
  EXPECT_TRUE(withTrailing.valid());
  EXPECT_EQ(withTrailing.bytes().size(), trailing.size());
  EXPECT_TRUE(equalBytes(withTrailing.encode("root"), bytes));

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
  using N = nbt::NbtParser;
  const std::string embedded{"a\0\xC3\xA9", 4};
  const auto root = nbt::Tag(nbt::Tag::Compound{{"byte", nbt::Tag(nbt::Tag::Byte{-2})},
                                                {"short", nbt::Tag(nbt::Tag::Short{-300})},
                                                {"int", nbt::Tag(nbt::Tag::Int{-70000})},
                                                {"long", nbt::Tag(nbt::Tag::Long{-9000000000LL})},
                                                {"float", nbt::Tag(nbt::Tag::Float{-1.25F})},
                                                {"double", nbt::Tag(nbt::Tag::Double{2.5})},
                                                {"text", nbt::Tag(std::string_view{embedded})}});
  const auto bytes = N(root).encode("root");
  const auto document = N::parse(bytes);
  auto tag = document.readTag();
  EXPECT_EQ(std::get<nbt::Tag::Byte>(tag.find("byte")->payload()), -2);
  EXPECT_EQ(std::get<nbt::Tag::Short>(tag.find("short")->payload()), -300);
  EXPECT_EQ(std::get<nbt::Tag::Int>(tag.find("int")->payload()), -70000);
  EXPECT_EQ(std::get<nbt::Tag::Long>(tag.find("long")->payload()), -9000000000LL);
  EXPECT_EQ(std::get<nbt::Tag::Float>(tag.find("float")->payload()), -1.25F);
  EXPECT_EQ(std::get<nbt::Tag::Double>(tag.find("double")->payload()), 2.5);
  EXPECT_EQ(std::get<nbt::Tag::String>(tag.find("text")->payload()), embedded);
  EXPECT_EQ(std::get<nbt::Tag::String>(tag.find("text")->payload()).size(), 4);
  EXPECT_TRUE(equalBytes(document.encode("root"), bytes));
}

TEST(CoreTests, NetworkFormat) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode(std::nullopt);
  nbt::Options options;
  options.named = false;
  N document = N::parse(bytes, options);
  EXPECT_EQ(document.status(), nbt::Status::Complete);
  EXPECT_EQ(document.root().type(), nbt::Type::Compound);
}

TEST(CoreTests, EveryTruncation) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    N document = N::parseAtMost(std::as_bytes(std::span(bytes).first(size)));
    EXPECT_EQ(document.status(), nbt::Status::NeedMoreData);
  }
}

TEST(CoreTests, EveryIncrementalTruncation) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode(std::nullopt);
  N document;
  for (const auto byte : bytes) {
    document.append(std::span<const std::byte>(&byte, 1));
  }
  EXPECT_EQ(document.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(document.encode(std::nullopt), bytes));
  auto tag = document.readTag();
  const auto &values = std::get<nbt::Tag::Array<nbt::Tag::Short>>(std::get<nbt::Tag::List>(tag.find("values")->payload()));
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(std::get<nbt::Tag::Float>(tag.find("nested")->find("value")->payload()), 1.5F);
}

TEST(CoreTests, IncrementalMatchesSingleParse) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode(std::nullopt);
  const std::span<const std::byte> view(bytes);
  nbt::Options options;
  options.named = false;

  const N whole = N::parse(view, options);
  ASSERT_EQ(whole.status(), nbt::Status::Complete);
  const auto wholeTag = whole.readTag();

  for (std::size_t split = 1; split < bytes.size(); ++split) {
    N chunked;
    chunked.append(view.first(split));
    chunked.append(view.subspan(split));
    ASSERT_EQ(chunked.status(), nbt::Status::Complete) << "split=" << split;
    EXPECT_TRUE(equalBytes(chunked.bytes(), bytes)) << "split=" << split;
    EXPECT_TRUE(equalBytes(chunked.encode(std::nullopt), bytes)) << "split=" << split;

    const auto chunkedTag = chunked.readTag();
    EXPECT_EQ(chunkedTag.payload() == wholeTag.payload(), true) << "split=" << split;
  }
}

TEST(CoreTests, MaterializedArrays) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  N document = N::parse(bytes);
  EXPECT_TRUE(document.valid());

  auto tag = document.readTag();
  const auto &scores = std::get<nbt::Tag::IntArray>(tag.find("scores")->payload());
  EXPECT_EQ(scores.size(), 3);
  EXPECT_EQ(scores[0], 10);
  EXPECT_EQ(scores[1], 20);
  EXPECT_EQ(scores[2], 30);
  std::int32_t scoreTotal = 0;
  for (const auto score : scores) {
    scoreTotal += score;
  }
  EXPECT_EQ(scoreTotal, 60);

  const auto &big = std::get<nbt::Tag::LongArray>(tag.find("big")->payload());
  EXPECT_EQ(big.size(), 2);
  EXPECT_EQ(big.front(), 100);
  EXPECT_EQ(big.back(), 200);

  EXPECT_EQ(tag.type(), nbt::Type::Compound);
}

TEST(CoreTests, MaterializedContainers) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  N document = N::parse(bytes);
  EXPECT_TRUE(document.valid());

  auto tag = document.readTag();
  const auto &values = std::get<nbt::Tag::Array<nbt::Tag::Short>>(std::get<nbt::Tag::List>(tag.find("values")->payload()));
  EXPECT_EQ(values.size(), 2);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);

  EXPECT_GE(tag.size(), 4);
  const auto *answer = tag.find("answer");
  ASSERT_NE(answer, nullptr);
  EXPECT_EQ(std::get<nbt::Tag::Int>(answer->payload()), 42);
  EXPECT_EQ(tag.find("missing"), nullptr);
  EXPECT_THROW((void)std::get<nbt::Tag::String>(answer->payload()), std::bad_variant_access);

  std::size_t count = 0;
  for (const auto &child : std::get<nbt::Tag::Compound>(tag.payload())) {
    (void)child;
    ++count;
  }
  EXPECT_EQ(count, tag.size());
}

TEST(CoreTests, TypedTagAccess) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  N document = N::parse(bytes);
  ASSERT_TRUE(document.valid());

  const auto root = document.root();
  EXPECT_EQ(root.name(), "root");
  EXPECT_EQ(root.type(), nbt::Type::Compound);
  EXPECT_GE(root.size(), 4);

  auto tag = document.readTag();
  EXPECT_EQ(std::get<nbt::Tag::Int>(tag.find("answer")->payload()), 42);
  EXPECT_EQ(std::get<nbt::Tag::String>(tag.find("name")->payload()), "Alex");

  const auto &scores = std::get<nbt::Tag::IntArray>(tag.find("scores")->payload());
  ASSERT_EQ(scores.size(), 3);
  EXPECT_EQ(scores[0], 10);
  EXPECT_EQ(scores[1], 20);
  EXPECT_EQ(scores[2], 30);

  nbt::Tag owningTag = root;
  EXPECT_EQ(owningTag.type(), nbt::Type::Compound);
  EXPECT_EQ(std::get<nbt::Tag::Int>(owningTag.find("answer")->payload()), 42);
  EXPECT_EQ(std::get<nbt::Tag::IntArray>(owningTag.find("scores")->payload())[2], 30);
}

TEST(CoreTests, Encode) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  N document = N::parse(bytes);

  nbt::Buffer buffer;
  document.encode(buffer, "root");
  EXPECT_TRUE(equalBytes(buffer, bytes));

  const std::string oversizedName(nbt::utils::MAX_STR_SIZE + 1, 'x');
  EXPECT_THROW((void)document.encode(oversizedName), std::length_error);
}

TEST(CoreTests, ContainerListsRoundTrip) {
  using N = nbt::NbtParser;

  const auto stringList = nbt::Tag(std::vector<nbt::Tag::String>{"a", "b"});
  const auto compoundItem = nbt::Tag(nbt::Tag::Compound{{"k", nbt::Tag(nbt::Tag::Int{1})}});
  const auto root = nbt::Tag(nbt::Tag::Compound{{"strings", nbt::Tag(std::vector<nbt::Tag>{stringList})}, {"compounds", nbt::Tag(std::vector<nbt::Tag>{compoundItem})}});

  const auto document = N::parse(N(root).encode("root"));
  auto tag = document.readTag();
  const auto &stringChildren = std::get<nbt::Tag::Array<nbt::Tag>>(std::get<nbt::Tag::List>(tag.find("strings")->payload()));
  EXPECT_EQ(std::get<nbt::Tag::Array<nbt::Tag::String>>(std::get<nbt::Tag::List>(stringChildren[0].payload())).size(), 2);
  const auto &compoundChildren = std::get<nbt::Tag::Array<nbt::Tag>>(std::get<nbt::Tag::List>(tag.find("compounds")->payload()));
  nbt::Tag compoundCopy = compoundChildren[0];
  EXPECT_EQ(std::get<nbt::Tag::Int>(compoundCopy.find("k")->payload()), 1);
}

TEST(CoreTests, EndListsRequireZeroElements) {
  using N = nbt::NbtParser;

  const auto emptyList = nbt::Tag(std::vector<nbt::Tag>{});
  const auto bytes = N(emptyList).encode("empty");
  const std::array expected{
      std::byte{static_cast<unsigned char>(nbt::Type::List)}, std::byte{0}, std::byte{5}, std::byte{'e'}, std::byte{'m'}, std::byte{'p'}, std::byte{'t'}, std::byte{'y'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_TRUE(equalBytes(bytes, expected));
  EXPECT_EQ(N::parse(bytes).root().size(), 0);

  const auto normalizedList = nbt::Tag(std::vector<nbt::Tag>{nbt::Tag{}});
  EXPECT_EQ(normalizedList.size(), 0);

  auto invalidList = nbt::Tag(std::vector<nbt::Tag>{nbt::Tag{}});
  invalidList.payload(nbt::Tag::List(std::vector<nbt::Tag>{nbt::Tag{}}));
  auto wrongElementType = nbt::Tag(std::vector<nbt::Tag>{nbt::Tag(nbt::Tag::Long{1})});
  wrongElementType.elementType(nbt::Type::Int);
#ifdef NBT_STRICT_MODE
  // Strict mode encodes invalid input as-is; the malformed bytes fail on parse.
  EXPECT_THROW((void)N::parse(N(invalidList).encode("empty")), nbt::Exception);
  const auto verbatim = N::parse(N(wrongElementType).encode("values"));
  EXPECT_EQ(verbatim.root().type(), nbt::Type::List);
  EXPECT_EQ(verbatim.root().elementType(), nbt::Type::Int);
  EXPECT_EQ(verbatim.root().size(), 1);
#else
  EXPECT_TRUE(equalBytes(N(invalidList).encode("empty"), bytes));

  const auto skipped = N::parse(N(wrongElementType).encode("values"));
  EXPECT_EQ(skipped.root().type(), nbt::Type::List);
  EXPECT_EQ(skipped.root().elementType(), nbt::Type::End);
  EXPECT_EQ(skipped.root().size(), 0);
#endif

  const nbt::Tag namedElement(std::vector<nbt::Tag::Short>{1});
  const auto encoded = N::parse(N(namedElement).encode("values"));
  const auto encodedTag = encoded.readTag();
  EXPECT_EQ(encodedTag.size(), 1);
  const auto &encodedValues = std::get<nbt::Tag::Array<nbt::Tag::Short>>(std::get<nbt::Tag::List>(encodedTag.payload()));
  EXPECT_EQ(encodedValues[0], 1);

  nbt::Tag compound(nbt::Tag::Compound{{"value", nbt::Tag(nbt::Tag::Int{2})}});
  compound.add(nbt::Tag{});
#ifdef NBT_STRICT_MODE
  EXPECT_THROW((void)N(compound).encode("root"), std::invalid_argument);
#else
  const auto pruned = N::parse(N(compound).encode("root"));
  auto prunedTag = pruned.readTag();
  EXPECT_EQ(prunedTag.size(), 1);
  EXPECT_EQ(std::get<nbt::Tag::Int>(prunedTag.find("value")->payload()), 2);
#endif
}

TEST(CoreTests, MalformedInput) {
  using N = nbt::NbtParser;
  using E = nbt::Exception;

  const std::array invalidType{std::byte{0x7f}};
  EXPECT_THROW((void)N::parse(invalidType), E);

  const std::array namedEnd{std::byte{0}};
  EXPECT_THROW((void)N::parse(namedEnd), E);

  const std::array negativeArrayLength{std::byte{static_cast<unsigned char>(nbt::Type::ByteArray)}, std::byte{0}, std::byte{0}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  EXPECT_THROW((void)N::parse(negativeArrayLength), E);

  // Parsing named bytes as unnamed is not an error: any root type is accepted and
  // the caller is responsible for interpreting the result correctly.
  auto scalar = N(nbt::Tag(nbt::Tag::Int{1}));
  nbt::Options network;
  network.named = false;
  const auto misread = N::parse(scalar.encode("value"), network);
  EXPECT_TRUE(misread.valid());
  EXPECT_EQ(misread.root().type(), nbt::Type::Int);
}

TEST(CoreTests, NamedFlag) {
  using N = nbt::NbtParser;

  const auto named = N(sample()).encode("root");
  const auto unnamed = N(sample()).encode(std::nullopt);
  EXPECT_EQ(unnamed.size(), named.size() - 2 - 4); // minus u16 length and "root"
  EXPECT_TRUE(equalBytes(std::span(unnamed).subspan(1), std::span(named).subspan(7)));

  const auto fromNamed = N::parse(named);
  EXPECT_TRUE(equalBytes(fromNamed.encode(std::nullopt), unnamed));
  EXPECT_TRUE(equalBytes(fromNamed.encode("root"), named));

  nbt::Options options;
  options.named = false;
  const auto fromUnnamed = N::parse(unnamed, options);
  EXPECT_TRUE(fromUnnamed.valid());
  EXPECT_EQ(fromUnnamed.root().name(), "");
  const auto renamed = fromUnnamed.encode("");
  EXPECT_EQ(renamed.size(), unnamed.size() + 2);
  EXPECT_TRUE(equalBytes(std::span(renamed).subspan(3), std::span(unnamed).subspan(1)));

  N incremental;
  incremental.append(std::span<const std::byte>(unnamed));
  EXPECT_EQ(incremental.status(), nbt::Status::Complete);
  EXPECT_EQ(incremental.root().type(), nbt::Type::Compound);

  const auto split = unnamed.size() / 2;
  auto parsedIncremental = N::parseAtMost(std::span(unnamed).first(split), options);
  EXPECT_EQ(parsedIncremental.status(), nbt::Status::NeedMoreData);
  parsedIncremental.append(std::span(unnamed).subspan(split));
  EXPECT_EQ(parsedIncremental.status(), nbt::Status::Complete);
}

TEST(CoreTests, NameRoundTrips) {
  using N = nbt::NbtParser;

  const auto namedEmpty = N(nbt::Tag(nbt::Tag::Compound{})).encode("");
  const auto parsedEmpty = N::parse(namedEmpty);
  EXPECT_EQ(parsedEmpty.root().name(), "");
  EXPECT_TRUE(equalBytes(parsedEmpty.encode(""), namedEmpty));

  const auto bytes = N(sample()).encode("root");
  const auto document = N::parse(bytes);
  auto tag = document.readTag();
  EXPECT_EQ(document.root().name(), "root");

  auto roundTrip = N::parse(document.encode("root")).readTag();
  EXPECT_EQ(roundTrip.find("answer")->type(), nbt::Type::Int);
  EXPECT_EQ(roundTrip.find("nested")->find("value")->type(), nbt::Type::Float);
}

TEST(CoreTests, NonCompoundRoot) {
  using N = nbt::NbtParser;

  const auto scalar = N(nbt::Tag(nbt::Tag::Int{7})).encode("solo");
  const auto document = N::parse(scalar);
  EXPECT_TRUE(document.valid());
  auto tag = document.readTag();
  EXPECT_EQ(tag.type(), nbt::Type::Int);
  EXPECT_EQ(std::get<nbt::Tag::Int>(tag.payload()), 7);

  nbt::Options options;
  options.named = false;
  const auto unnamedScalar = N(nbt::Tag(nbt::Tag::Int{7})).encode(std::nullopt);
  const auto unnamedDocument = N::parse(unnamedScalar, options);
  EXPECT_TRUE(unnamedDocument.valid());
  EXPECT_EQ(std::get<nbt::Tag::Int>(unnamedDocument.readTag().payload()), 7);
}

TEST(CoreTests, TruncatedParseThrowsNeedMoreData) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  EXPECT_THROW((void)N::parse(std::span<const std::byte>(bytes).first(bytes.size() - 1)), nbt::NeedMoreDataException);
  EXPECT_THROW((void)N::parse(std::span<const std::byte>{}), nbt::NeedMoreDataException);
}

TEST(CoreTests, EncodeErrors) {
  using N = nbt::NbtParser;

  N empty;
  EXPECT_THROW((void)empty.encode(), nbt::Exception);

  N endRoot{nbt::Tag{}};
  EXPECT_THROW((void)endRoot.encode(), std::invalid_argument);

  const std::array incompleteBytes{std::byte{0x0A}};
  N incomplete = N::parseAtMost(std::span<const std::byte>{incompleteBytes});
  EXPECT_THROW((void)incomplete.encode(), nbt::Exception);
}

TEST(CoreTests, HandBuiltTagLimits) {
  using N = nbt::NbtParser;

  nbt::Tag deep = nbt::Tag(nbt::Tag::Int{1});
  for (int index = 0; index < 64; ++index) {
    deep = nbt::Tag(nbt::Tag::Compound{{"level" + std::to_string(index), std::move(deep)}});
  }

  N document{std::move(deep)};
  const auto encoded = document.encode("root");
  const auto reparsed = N::parse(encoded);
  EXPECT_EQ(reparsed.status(), nbt::Status::Complete);
  EXPECT_TRUE(equalBytes(reparsed.encode("root"), encoded));

  N built{nbt::Tag(nbt::Tag::Int{1})};
  EXPECT_THROW(built.append(std::span<const std::byte>{std::array{std::byte{0x0A}}}), std::logic_error);
}

TEST(CoreTests, MaterializeAllTypes) {
  using N = nbt::NbtParser;
  const auto tag = N::parse(N(sample()).encode("root")).readTag();
  EXPECT_EQ(tag.type(), nbt::Type::Compound);
  const auto &children = std::get<nbt::Tag::Compound>(tag.payload());
  EXPECT_EQ(std::get<nbt::Tag::Int>(children.find("answer")->second.payload()), 42);
  EXPECT_EQ(std::get<nbt::Tag::String>(children.find("name")->second.payload()), "Alex");
  EXPECT_EQ(children.find("values")->second.elementType(), nbt::Type::Short);
  EXPECT_EQ(std::get<nbt::Tag::IntArray>(children.find("scores")->second.payload()).size(), 3);
  EXPECT_EQ(std::get<nbt::Tag::LongArray>(children.find("big")->second.payload())[1], 200);
}

TEST(CoreTests, CompoundIndexAndIteration) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  const auto document = N::parse(bytes);
  const auto root = document.root();

  std::vector<std::string> names;
  for (const auto &child : root) {
    names.emplace_back(child.name());
  }
  EXPECT_EQ(names.size(), root.size());
  EXPECT_NE(std::find(names.begin(), names.end(), "answer"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "nested"), names.end());
}

TEST(CoreTests, ListIndexAndIteration) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  const auto document = N::parse(bytes);
  auto rootTag = document.readTag();
  const auto *values = rootTag.find("values");
  ASSERT_NE(values, nullptr);

  const auto &list = std::get<nbt::Tag::List>(values->payload());
  const auto &shortValues = std::get<nbt::Tag::Array<nbt::Tag::Short>>(list);
  ASSERT_EQ(shortValues.size(), 2);
  EXPECT_EQ(shortValues[0], 1);
  EXPECT_EQ(shortValues[1], 2);

  std::vector<nbt::Tag::Short> numbers;
  const auto view = document.root();
  // The values node is a sibling of the root children; iterate from the values view via the document root.
  for (const auto &child : view) {
    if (child.name() == "values") {
      for (const auto &entry : child) {
        EXPECT_EQ(entry.type(), nbt::Type::Short);
        EXPECT_EQ(entry.elementType(), nbt::Type::End);
        EXPECT_TRUE(entry.name().empty());
        EXPECT_EQ(entry.size(), 0);
        EXPECT_THROW((void)child.as<nbt::Type::Short>(), std::bad_variant_access);
        numbers.push_back(entry.as<nbt::Type::Short>());
        EXPECT_EQ(std::get<nbt::Tag::Short>(static_cast<nbt::Tag>(entry).payload()), numbers.back());
      }
      break;
    }
  }
  EXPECT_EQ(numbers, (std::vector<nbt::Tag::Short>{1, 2}));
}

TEST(CoreTests, ArrayViewsExposeElementViews) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");
  const auto document = N::parse(bytes);
  const auto scores = document.root();

  std::size_t size = 0;
  for (const auto &child : scores) {
    if (child.name() == "scores") {
      size = child.size();
      const auto values = child.as<nbt::Type::IntArray>();
      EXPECT_EQ(values.size(), 3);
      EXPECT_EQ(values[0], 10);
      const auto first = child.child(0);
      ASSERT_TRUE(first);
      EXPECT_EQ(first.type(), nbt::Type::Int);
      EXPECT_EQ(first.as<nbt::Type::Int>(), 10);
      EXPECT_EQ(std::get<nbt::Tag::Int>(static_cast<nbt::Tag>(first).payload()), 10);
      std::vector<nbt::Tag::Int> iterated;
      for (const auto &entry : child) {
        iterated.push_back(entry.as<nbt::Type::Int>());
      }
      EXPECT_EQ(iterated, values);
      EXPECT_THROW((void)child.as<nbt::Type::Int>(), std::bad_variant_access);
    }
  }
  EXPECT_EQ(size, 3);
}

TEST(CoreTests, DocumentMoveKeepsViewsValid) {
  using N = nbt::NbtParser;
  const auto bytes = N(sample()).encode("root");

  N document = N::parse(bytes);
  const auto root = document.root();

  N moved = std::move(document);
  EXPECT_FALSE(document.valid());

  const auto movedRoot = moved.root();
  EXPECT_EQ(movedRoot.type(), nbt::Type::Compound);

  // The original view survives the move because its lifetime travels with the data.
  EXPECT_EQ(root.type(), nbt::Type::Compound);
}

// ---------------------------------------------------------------------------
// Owning Tag tests: construction, containers and lookup only.
// ---------------------------------------------------------------------------

TEST(CoreTests, TagEndIsEmpty) {
  nbt::Tag tag;
  EXPECT_EQ(tag.type(), nbt::Type::End);
  EXPECT_EQ(tag.elementType(), nbt::Type::End);
  EXPECT_FALSE(static_cast<bool>(tag));
  EXPECT_EQ(tag.size(), 0);
  EXPECT_EQ(tag.find("anything"), nullptr);
  EXPECT_TRUE(std::holds_alternative<nbt::Tag::End>(tag.payload()));
}

TEST(CoreTests, TagScalars) {
  const nbt::Tag byte(nbt::Tag::Byte{-128});
  EXPECT_EQ(byte.type(), nbt::Type::Byte);
  EXPECT_EQ(std::get<nbt::Tag::Byte>(byte.payload()), -128);
  EXPECT_EQ(byte.size(), 0);

  const nbt::Tag shortTag(nbt::Tag::Short{-32768});
  EXPECT_EQ(shortTag.type(), nbt::Type::Short);
  EXPECT_EQ(std::get<nbt::Tag::Short>(shortTag.payload()), -32768);

  const nbt::Tag intTag(nbt::Tag::Int{0});
  EXPECT_EQ(intTag.type(), nbt::Type::Int);
  EXPECT_EQ(std::get<nbt::Tag::Int>(intTag.payload()), 0);

  const nbt::Tag longTag((std::numeric_limits<nbt::Tag::Long>::min)());
  EXPECT_EQ(longTag.type(), nbt::Type::Long);
  EXPECT_EQ(std::get<nbt::Tag::Long>(longTag.payload()), (std::numeric_limits<nbt::Tag::Long>::min)());

  const nbt::Tag floatTag(nbt::Tag::Float{-0.0F});
  EXPECT_EQ(floatTag.type(), nbt::Type::Float);
  EXPECT_TRUE(std::signbit(std::get<nbt::Tag::Float>(floatTag.payload())));

  const nbt::Tag doubleTag((std::numeric_limits<nbt::Tag::Double>::quiet_NaN)());
  EXPECT_EQ(doubleTag.type(), nbt::Type::Double);
  EXPECT_TRUE(std::isnan(std::get<nbt::Tag::Double>(doubleTag.payload())));

  const nbt::Tag infinity((std::numeric_limits<nbt::Tag::Float>::infinity)());
  EXPECT_TRUE(std::isinf(std::get<nbt::Tag::Float>(infinity.payload())));

  for (auto tagCopy : {byte, shortTag, intTag, longTag, floatTag, doubleTag}) {
    EXPECT_TRUE(static_cast<bool>(tagCopy));
    EXPECT_EQ(tagCopy.elementType(), nbt::Type::End);
    EXPECT_EQ(tagCopy.find("x"), nullptr);
  }
}

TEST(CoreTests, TagStrings) {
  const nbt::Tag empty(std::string_view{});
  EXPECT_EQ(empty.type(), nbt::Type::String);
  EXPECT_TRUE(std::get<nbt::Tag::String>(empty.payload()).empty());

  const nbt::Tag utf8(std::string_view{"\xC3\xA9"});
  EXPECT_EQ(std::get<nbt::Tag::String>(utf8.payload()).size(), 2);

  const std::string embedded{"a\0b", 3};
  const nbt::Tag nul(std::string_view{embedded});
  EXPECT_EQ(std::get<nbt::Tag::String>(nul.payload()).size(), 3);
  EXPECT_EQ(std::get<nbt::Tag::String>(nul.payload())[1], '\0');

  const nbt::Tag owned(std::string("text"));
  EXPECT_EQ(std::get<nbt::Tag::String>(owned.payload()), "text");
}

TEST(CoreTests, TagArrays) {
  const nbt::Tag bytes(std::vector<nbt::Tag::Byte>{-128, 0, 127});
  EXPECT_EQ(bytes.type(), nbt::Type::ByteArray);
  EXPECT_EQ(bytes.elementType(), nbt::Type::Byte);
  EXPECT_EQ(bytes.size(), 3);
  EXPECT_EQ(std::get<nbt::Tag::ByteArray>(bytes.payload())[0], -128);

  const nbt::Tag ints(std::vector<nbt::Tag::Int>{(std::numeric_limits<nbt::Tag::Int>::min)()});
  EXPECT_EQ(ints.type(), nbt::Type::IntArray);
  EXPECT_EQ(ints.size(), 1);

  const nbt::Tag longs(std::vector<nbt::Tag::Long>{(std::numeric_limits<nbt::Tag::Long>::max)()});
  EXPECT_EQ(longs.type(), nbt::Type::LongArray);
  EXPECT_EQ(longs.size(), 1);

  const nbt::Tag emptyArray(std::vector<nbt::Tag::Byte>{});
  EXPECT_EQ(emptyArray.type(), nbt::Type::ByteArray);
  EXPECT_EQ(emptyArray.size(), 0);
}

TEST(CoreTests, TagScalarVectorsBecomeLists) {
  const nbt::Tag shorts(std::vector<nbt::Tag::Short>{1, 2});
  EXPECT_EQ(shorts.type(), nbt::Type::List);
  EXPECT_EQ(shorts.elementType(), nbt::Type::Short);
  EXPECT_EQ(shorts.size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::Array<nbt::Tag::Short>>(std::get<nbt::Tag::List>(shorts.payload()))[1], 2);

  const nbt::Tag floats(std::vector<nbt::Tag::Float>{1.5F});
  EXPECT_EQ(floats.type(), nbt::Type::List);
  EXPECT_EQ(floats.elementType(), nbt::Type::Float);

  const nbt::Tag doubles(std::vector<nbt::Tag::Double>{2.5});
  EXPECT_EQ(doubles.type(), nbt::Type::List);
  EXPECT_EQ(doubles.elementType(), nbt::Type::Double);

  const nbt::Tag strings(std::vector<nbt::Tag::String>{"a", "b"});
  EXPECT_EQ(strings.type(), nbt::Type::List);
  EXPECT_EQ(strings.elementType(), nbt::Type::String);
  EXPECT_EQ(std::get<nbt::Tag::Array<nbt::Tag::String>>(std::get<nbt::Tag::List>(strings.payload()))[0], "a");

  const nbt::Tag bytes(std::vector<nbt::Tag::Byte>{1, 2}, true);
  const nbt::Tag ints(std::vector<nbt::Tag::Int>{3, 4}, true);
  const nbt::Tag longs(std::vector<nbt::Tag::Long>{5, 6}, true);
  EXPECT_EQ(bytes.size(), 2);
  EXPECT_EQ(ints.size(), 2);
  EXPECT_EQ(longs.size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::Array<nbt::Tag::Byte>>(std::get<nbt::Tag::List>(bytes.payload()))[1], 2);
  EXPECT_EQ(std::get<nbt::Tag::Array<nbt::Tag::Int>>(std::get<nbt::Tag::List>(ints.payload()))[1], 4);
  EXPECT_EQ(std::get<nbt::Tag::Array<nbt::Tag::Long>>(std::get<nbt::Tag::List>(longs.payload()))[1], 6);
}

TEST(CoreTests, TagListOfTags) {
  nbt::Tag list;
  list.add(nbt::Tag(nbt::Tag::Compound{{"x", nbt::Tag(nbt::Tag::Int{1})}}));
  list.add(nbt::Tag(nbt::Tag::Compound{{"x", nbt::Tag(nbt::Tag::Int{2})}}));
  EXPECT_EQ(list.type(), nbt::Type::List);
  EXPECT_EQ(list.elementType(), nbt::Type::Compound);
  EXPECT_EQ(list.size(), 2);

  const nbt::Tag emptyList(std::vector<nbt::Tag>{});
  EXPECT_EQ(emptyList.type(), nbt::Type::List);
  EXPECT_EQ(emptyList.elementType(), nbt::Type::End);
  EXPECT_EQ(emptyList.size(), 0);

#ifndef NBT_STRICT_MODE
  // Non-strict keeps heterogeneous children; the encoder filters them later.
  const nbt::Tag mixed(std::vector<nbt::Tag>{nbt::Tag(nbt::Tag::Compound{}), nbt::Tag(std::vector<nbt::Tag>{})});
  EXPECT_EQ(mixed.type(), nbt::Type::List);
  EXPECT_EQ(mixed.elementType(), nbt::Type::Compound);
  EXPECT_EQ(mixed.size(), 2);
#else
  EXPECT_THROW((void)nbt::Tag(std::vector<nbt::Tag>{nbt::Tag(nbt::Tag::Compound{}), nbt::Tag(std::vector<nbt::Tag>{})}), nbt::Exception);
#endif
}

TEST(CoreTests, TagNestedContainerVectors) {
  const auto innerList = nbt::Tag(std::vector<nbt::Tag::String>{"a"});
  const nbt::Tag listOfLists(std::vector<nbt::Tag>{innerList});
  EXPECT_EQ(listOfLists.type(), nbt::Type::List);
  EXPECT_EQ(listOfLists.elementType(), nbt::Type::List);

  const auto innerCompound = nbt::Tag(nbt::Tag::Compound{{"k", nbt::Tag(nbt::Tag::Byte{1})}});
  const nbt::Tag listOfCompounds(std::vector<nbt::Tag>{innerCompound});
  EXPECT_EQ(listOfCompounds.type(), nbt::Type::List);
  EXPECT_EQ(listOfCompounds.elementType(), nbt::Type::Compound);

  const nbt::Tag listOfArrays(std::vector<nbt::Tag::ByteArray>{{1, 2}, {3}});
  EXPECT_EQ(listOfArrays.type(), nbt::Type::List);
  EXPECT_EQ(listOfArrays.elementType(), nbt::Type::ByteArray);
  EXPECT_EQ(listOfArrays.size(), 2);
}

TEST(CoreTests, TagCompound) {
  nbt::Tag compound(nbt::Tag::Compound{{"a", nbt::Tag(nbt::Tag::Int{1})}, {"b", nbt::Tag(nbt::Tag::Byte{2})}});
  EXPECT_EQ(compound.type(), nbt::Type::Compound);
  EXPECT_EQ(compound.size(), 2);

  const nbt::Tag *a = compound.find("a");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->type(), nbt::Type::Int);
  EXPECT_EQ(std::get<nbt::Tag::Int>(a->payload()), 1);

  EXPECT_EQ(compound.find("missing"), nullptr);

  nbt::Tag nested(nbt::Tag::Compound{{"outer", compound}});
  ASSERT_NE(nested.find("outer"), nullptr);
  EXPECT_EQ(nested.find("outer")->find("b")->type(), nbt::Type::Byte);

  // Names only exist as compound keys; scalars and lists never carry one.
  nbt::Tag scalar(nbt::Tag::Int{1});
  EXPECT_EQ(scalar.find("a"), nullptr);
  nbt::Tag list(std::vector<nbt::Tag>{nbt::Tag(nbt::Tag::Int{1})});
  EXPECT_EQ(list.find("a"), nullptr);
}

TEST(CoreTests, TagInitializerLists) {
  const nbt::Tag ints{nbt::Tag::Int{1}, nbt::Tag::Int{2}};
  EXPECT_EQ(ints.type(), nbt::Type::IntArray);
  EXPECT_EQ(ints.size(), 2);

  const nbt::Tag bytes{nbt::Tag::Byte{1}};
  EXPECT_EQ(bytes.type(), nbt::Type::ByteArray);

  const nbt::Tag tags{nbt::Tag(nbt::Tag::Int{1}), nbt::Tag(nbt::Tag::Int{2})};
  EXPECT_EQ(tags.type(), nbt::Type::List);
  EXPECT_EQ(tags.elementType(), nbt::Type::Int);

  nbt::Tag compound{{"x", nbt::Tag(nbt::Tag::Int{1})}, {"y", nbt::Tag(nbt::Tag::Byte{2})}};
  EXPECT_EQ(compound.type(), nbt::Type::Compound);
  EXPECT_EQ(compound.size(), 2);
  EXPECT_NE(compound.find("x"), nullptr);
}

TEST(CoreTests, TagCopyMoveAndAssign) {
  const nbt::Tag source(nbt::Tag::Compound{{"k", nbt::Tag(nbt::Tag::Int{3})}});

  nbt::Tag copied = source;
  EXPECT_EQ(copied.type(), nbt::Type::Compound);
  EXPECT_EQ(std::get<nbt::Tag::Int>(copied.find("k")->payload()), 3);

  nbt::Tag assigned;
  assigned = source;
  EXPECT_EQ(assigned.type(), nbt::Type::Compound);
  EXPECT_NE(assigned.find("k"), nullptr);

  nbt::Tag moved = std::move(copied);
  EXPECT_EQ(moved.type(), nbt::Type::Compound);

  nbt::Tag moveAssigned;
  moveAssigned = nbt::Tag(nbt::Tag::Int{9});
  EXPECT_EQ(moveAssigned.type(), nbt::Type::Int);
}

TEST(CoreTests, TagAddToArrays) {
  nbt::Tag bytes(std::vector<nbt::Tag::Byte>{1});
  bytes.add(nbt::Tag::Byte{2});
  bytes.add(nbt::Tag::Byte{3});
  EXPECT_EQ(bytes.type(), nbt::Type::ByteArray);
  EXPECT_EQ(bytes.size(), 3);
  EXPECT_EQ(std::get<nbt::Tag::ByteArray>(bytes.payload())[2], 3);

#ifdef NBT_STRICT_MODE
  EXPECT_THROW(bytes.add(nbt::Tag::Int{1}), nbt::Exception);
#else
  bytes.add(nbt::Tag::Int{1});
  EXPECT_EQ(bytes.size(), 3);
#endif

  nbt::Tag ints(std::vector<nbt::Tag::Int>{});
  ints.add(nbt::Tag::Int{-5});
  EXPECT_EQ(ints.size(), 1);
  EXPECT_EQ(std::get<nbt::Tag::IntArray>(ints.payload())[0], -5);

  nbt::Tag longs;
  longs.add(nbt::Tag::Long{42}); // End tag adopts the array implied by the value.
  EXPECT_EQ(longs.type(), nbt::Type::LongArray);
  EXPECT_EQ(longs.size(), 1);
}

TEST(CoreTests, TagAddToLists) {
  nbt::Tag list;
  list.add(nbt::Tag(nbt::Tag::Short{1}));
  EXPECT_EQ(list.elementType(), nbt::Type::Short);
  list.add(nbt::Tag(nbt::Tag::Short{2}));
  EXPECT_EQ(list.size(), 2);

#ifdef NBT_STRICT_MODE
  EXPECT_THROW(list.add(nbt::Tag(nbt::Tag::Byte{1})), nbt::Exception);
#else
  list.add(nbt::Tag(nbt::Tag::Byte{1}));
  EXPECT_EQ(list.size(), 2);
#endif

  nbt::Tag nested;
  const nbt::Tag inner(std::vector<nbt::Tag::String>{"a"});
  nested.add(inner);
  nested.add(inner);
  EXPECT_EQ(nested.elementType(), nbt::Type::List);
}

TEST(CoreTests, TagAddToCompounds) {
  nbt::Tag compound;
  compound.add("x", nbt::Tag(nbt::Tag::Int{7}));
  EXPECT_EQ(compound.type(), nbt::Type::Compound);
  ASSERT_NE(compound.find("x"), nullptr);

  compound.add("y", nbt::Tag(nbt::Tag::Byte{2}));
  EXPECT_EQ(compound.size(), 2);

  compound.add("x", nbt::Tag(nbt::Tag::Byte{1})); // Duplicate names replace.
  EXPECT_EQ(compound.size(), 2);
  EXPECT_EQ(compound.find("x")->type(), nbt::Type::Byte);

#ifdef NBT_STRICT_MODE
  EXPECT_THROW(compound.add("bad", nbt::Tag{}), nbt::Exception);
#else
  compound.add("bad", nbt::Tag{});
  EXPECT_EQ(compound.size(), 2);
#endif

  nbt::Tag notCompound(nbt::Tag::Int{1});
#ifdef NBT_STRICT_MODE
  EXPECT_THROW(notCompound.add("k", nbt::Tag(nbt::Tag::Int{1})), nbt::Exception);
#else
  notCompound.add("k", nbt::Tag(nbt::Tag::Int{1}));
  EXPECT_EQ(notCompound.type(), nbt::Type::Int);
#endif
}

TEST(CoreTests, TagAddRejectsInvalidTargets) {
  nbt::Tag scalar(nbt::Tag::Int{1});
#ifdef NBT_STRICT_MODE
  EXPECT_THROW(scalar.add(nbt::Tag::Int{2}), nbt::Exception);
#else
  scalar.add(nbt::Tag::Int{2});
  EXPECT_EQ(scalar.type(), nbt::Type::Int);
#endif

  nbt::Tag end;
#ifdef NBT_STRICT_MODE
  EXPECT_THROW(end.add(nbt::Tag{}), nbt::Exception);
#else
  end.add(nbt::Tag{});
  EXPECT_EQ(end.type(), nbt::Type::End);
#endif
}

TEST(CoreTests, TagNamePairs) {
  using namespace nbt::tag_literals;

  const auto named = "key" | nbt::Tag(nbt::Tag::Int{5});
  static_assert(std::is_same_v<std::decay_t<decltype(named.first)>, std::string>);
  EXPECT_EQ(named.first, "key");
  EXPECT_EQ(named.second.type(), nbt::Type::Int);

  nbt::Tag compound;
  compound.add("a", nbt::Tag(nbt::Tag::Int{1}));
  compound.add("b", nbt::Tag(nbt::Tag::Byte{2}));
  ASSERT_EQ(compound.type(), nbt::Type::Compound);
  EXPECT_EQ(compound.size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::Int>(compound.find("a")->payload()), 1);
}
