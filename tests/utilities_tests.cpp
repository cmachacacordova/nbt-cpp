#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
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
  return nbt::Tag("root",
                  std::vector<nbt::Tag>{nbt::Tag("answer", int32_t(42)),
                                        nbt::Tag("name", "Alex"s),
                                        nbt::Tag("values", nbt::Type::Int, {nbt::Tag("", int32_t(1)), nbt::Tag("", int32_t(2))}),
                                        nbt::Tag("scores", std::vector<nbt::Tag::Int>{10, 20, 30}),
                                        nbt::Tag("big", std::vector<nbt::Tag::Long>{std::int64_t{100}, std::int64_t{200}}),
                                        nbt::Tag("nested", std::vector<nbt::Tag>{nbt::Tag("value", float(1.5))})});
}

} // namespace

TEST(UtilitiesTests, SnbtValuesAndFormatting) {
  using U = nbt::NbtUtilities;

  const auto value = U::parseSnbt("{name:\"Alex\",health:20s,enabled:true,values:[1,2,3],bytes:[B;1b,-2b],ints:[I;3,4],longs:[L;5L]}");
  EXPECT_EQ(value.type, nbt::Type::Compound);
  const auto &children = std::get<nbt::Tag::Container>(value.payload);
  EXPECT_EQ(children.size(), 7);
  EXPECT_EQ(children[0].type, nbt::Type::String);
  EXPECT_EQ(children[1].type, nbt::Type::Short);
  EXPECT_EQ(children[2].type, nbt::Type::Int);
  EXPECT_EQ(children[3].type, nbt::Type::List);
  EXPECT_EQ(children[4].type, nbt::Type::ByteArray);
  EXPECT_EQ(children[5].type, nbt::Type::IntArray);
  EXPECT_EQ(children[6].type, nbt::Type::LongArray);
  EXPECT_EQ(std::get<nbt::Tag::ByteArray>(children[4].payload).size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::IntArray>(children[5].payload).size(), 2);
  EXPECT_EQ(std::get<nbt::Tag::LongArray>(children[6].payload).size(), 1);

  const auto compact = U::toSnbt(value);
  const auto pretty = U::toSnbt(value, true);
  EXPECT_FALSE(compact.empty());
  EXPECT_NE(pretty.find('\n'), std::string::npos);
  EXPECT_EQ(U::parseSnbt(compact).type, nbt::Type::Compound);
}

TEST(UtilitiesTests, SnbtErrorsAndLimits) {
  using U = nbt::NbtUtilities;

  EXPECT_ANY_THROW((void)U::parseSnbt("{name:\"\\q\"}"));
  EXPECT_ANY_THROW((void)U::parseSnbt("1 2"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[1,2s]"));
  EXPECT_ANY_THROW((void)U::parseSnbt("[B;not-a-number]"));

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
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;

  const auto snbt = U::toSnbt(sample());
  auto decoded = U::parseSnbt(snbt);
  decoded.name = "root";
  EXPECT_TRUE(equalBytes(N(decoded).encode(), N(sample()).encode()));
}

TEST(UtilitiesTests, CompressedFileRoundTrip) {
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-lazy-test.dat";
  N document(sample());
  for (const auto compression : {U::Compression::None, U::Compression::Gzip, U::Compression::Zlib}) {
    U::saveFile(path, document, compression);
    auto loaded = U::parseFile(path, compression);
    auto autoLoaded = U::parseFile(path);
    EXPECT_EQ(loaded.root().find("answer").as<nbt::Type::Int>(), 42);
    EXPECT_EQ(autoLoaded.root().find("name").as<nbt::Type::String>(), "Alex");
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
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-unnamed-test.dat";
  N document(sample());
  U::saveFile(path, document, U::Compression::None, false);

  nbt::Options options;
  options.named = false;
  auto loaded = U::parseFile(path, U::Compression::None, options);
  EXPECT_TRUE(loaded.valid());
  EXPECT_EQ(loaded.root().name(), "");
  EXPECT_EQ(loaded.root().find("answer").as<nbt::Type::Int>(), 42);
  std::filesystem::remove(path);
}
