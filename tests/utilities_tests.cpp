#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "nbt/nbt.h"
#include "nbt/utilities.h"

#include "gtest/gtest.h"

namespace {

bool equalBytes(const auto &left, const auto &right) {
  return left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size()) == 0;
}

nbt::Tag sample() {
  using namespace std::string_literals;
  const nbt::Tag values(std::vector<nbt::Tag::Short>{1, 2});
  return nbt::Tag(nbt::Tag::Compound{
      {"answer"s, nbt::Tag(nbt::Tag::Int{42})},
      {"name"s, nbt::Tag("Alex"s)},
      {"values"s, values},
      {"scores"s, nbt::Tag(std::vector<nbt::Tag::Int>{10, 20, 30})},
      {"big"s, nbt::Tag(std::vector<nbt::Tag::Long>{std::int64_t{100}, std::int64_t{200}})},
      {"nested"s, nbt::Tag(nbt::Tag::Compound{{"value"s, nbt::Tag(1.5f)}})}});
}

} // namespace

TEST(UtilitiesTests, SnbtValuesAndFormatting) {
  using U = nbt::NbtUtilities;

  const auto value = U::parseSnbt("{name:\"Alex\",health:20s,enabled:true,values:[1s,2s,3s],bytes:[B;1b,-2b],ints:[I;3,4],longs:[L;5L]}");
  EXPECT_EQ(value.type(), nbt::Type::Compound);
  const auto &children = std::get<nbt::Tag::Compound>(value.payload());
  EXPECT_EQ(children.size(), 7);

  const auto findType = [&](const char *name) {
    const auto it = children.find(name);
    EXPECT_NE(it, children.end());
    return it != children.end() ? it->second.type() : nbt::Type::End;
  };

  EXPECT_EQ(findType("name"), nbt::Type::String);
  EXPECT_EQ(findType("health"), nbt::Type::Short);
  EXPECT_EQ(findType("enabled"), nbt::Type::Byte);
  EXPECT_EQ(findType("values"), nbt::Type::List);
  EXPECT_EQ(findType("bytes"), nbt::Type::ByteArray);
  EXPECT_EQ(findType("ints"), nbt::Type::IntArray);
  EXPECT_EQ(findType("longs"), nbt::Type::LongArray);

  EXPECT_EQ(std::get<nbt::Tag::String>(children.find("name")->second.payload()), "Alex");
  EXPECT_EQ(std::get<nbt::Tag::Short>(children.find("health")->second.payload()), 20);
  EXPECT_EQ(std::get<nbt::Tag::Byte>(children.find("enabled")->second.payload()), 1);

  const auto &values = std::get<nbt::Tag::Array<nbt::Tag::Short>>(std::get<nbt::Tag::List>(children.find("values")->second.payload()));
  EXPECT_EQ(values.size(), 3);

  EXPECT_EQ(std::get<nbt::Tag::ByteArray>(children.find("bytes")->second.payload()).size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::IntArray>(children.find("ints")->second.payload()).size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::LongArray>(children.find("longs")->second.payload()).size(), 1);

  const auto compact = U::toSnbt(value);
  const auto pretty = U::toSnbt(value, true);
  EXPECT_FALSE(compact.empty());
  EXPECT_NE(pretty.find('\n'), std::string::npos);
  EXPECT_EQ(U::parseSnbt(compact).type(), nbt::Type::Compound);
}

TEST(UtilitiesTests, SnbtErrorsAndLimits) {
  using U = nbt::NbtUtilities;

  EXPECT_ANY_THROW((void)U::parseSnbt("{name:\"\\q\"}"));
  EXPECT_ANY_THROW((void)U::parseSnbt("1 2"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[1,2s]"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[B;not-a-number]"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[I;1b]"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[B;1L]"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[L;1s]"));

  nbt::Options depthLimit;
  depthLimit.maxDepth = 0;
  EXPECT_THROW((void)U::parseSnbt("{nested:{value:1}}", depthLimit), nbt::Exception);

  nbt::Options elementLimit;
  elementLimit.maxContainerElements = 1;
  EXPECT_THROW((void)U::parseSnbt("{first:1,second:2}", elementLimit), nbt::Exception);

  nbt::Options inputLimit;
  inputLimit.maxInputBytes = 2;
  EXPECT_THROW((void)U::parseSnbt("123", inputLimit), nbt::Exception);

  nbt::Options nodeLimit;
  nodeLimit.maxTotalNodes = 1;
  EXPECT_THROW((void)U::parseSnbt("{value:1}", nodeLimit), nbt::Exception);
}

TEST(UtilitiesTests, SnbtRoundTrip) {
  using N = nbt::NbtParser;
  using U = nbt::NbtUtilities;

  const auto snbt = U::toSnbt(sample());
  auto decoded = U::parseSnbt(snbt);
  EXPECT_TRUE(equalBytes(N(decoded).encode(), N(sample()).encode()));
}

TEST(UtilitiesTests, SnbtEscapesRoundTrip) {
  using U = nbt::NbtUtilities;

  const auto parsed = U::parseSnbt(R"({text:"a\"b\\c\nd\te\rb\bf\fg",plain:'raw'})");
  const auto &children = std::get<nbt::Tag::Compound>(parsed.payload());
  const auto &text = std::get<std::string>(children.find("text")->second.payload());
  EXPECT_EQ(text, "a\"b\\c\nd\te\rb\bf\fg");
  EXPECT_EQ(std::get<std::string>(children.find("plain")->second.payload()), "raw");

  const auto reparsed = U::parseSnbt(U::toSnbt(parsed));
  EXPECT_TRUE(equalBytes(nbt::NbtParser(reparsed).encode(), nbt::NbtParser(parsed).encode()));
}

TEST(UtilitiesTests, CompressedFileRoundTrip) {
  using N = nbt::NbtParser;
  using U = nbt::NbtUtilities;

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-lazy-test.dat";
  N document(sample());
  for (const auto compression : {U::Compression::None, U::Compression::Gzip, U::Compression::Zlib}) {
    U::saveFile(path, document, compression);
    auto loaded = U::parseFile(path, compression);
    auto autoLoaded = U::parseFile(path);
    EXPECT_EQ(std::get<nbt::Tag::Int>(loaded.readTag().find("answer")->payload()), 42);
    EXPECT_EQ(std::get<nbt::Tag::String>(autoLoaded.readTag().find("name")->payload()), "Alex");
  }

  EXPECT_THROW(U::saveFile(path, document, U::Compression::Auto), std::invalid_argument);

  EXPECT_THROW((void)U::parseFile(path.string() + ".missing"), std::runtime_error);

  U::saveFile(path, document, U::Compression::None);
  nbt::Options inputLimit;
  inputLimit.maxInputBytes = document.encode().size() - 1;
  EXPECT_THROW((void)U::parseFile(path, U::Compression::None, inputLimit), nbt::Exception);
  std::filesystem::remove(path);
}

TEST(UtilitiesTests, UnnamedFileRoundTrip) {
  using N = nbt::NbtParser;
  using U = nbt::NbtUtilities;

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-unnamed-test.dat";
  N document(sample());
  U::saveFile(path, document, U::Compression::None, false);

  nbt::Options options;
  options.named = false;
  auto loaded = U::parseFile(path, U::Compression::None, options);
  EXPECT_TRUE(loaded.valid());
  EXPECT_EQ(loaded.root().name(), "");
  auto tag = loaded.readTag();
  EXPECT_EQ(std::get<nbt::Tag::Int>(tag.find("answer")->payload()), 42);
  std::filesystem::remove(path);
}
