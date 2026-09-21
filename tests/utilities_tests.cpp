#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nbt/nbt.h"
#include "nbt/utilities.h"

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

void testSnbtValuesAndFormatting() {
  using U = nbt::NbtUtilities;

  const auto value = U::parseSnbt("{name:\"Alex\",health:20s,enabled:true,values:[1,2,3],bytes:[B;1b,-2b],ints:[I;3,4],longs:[L;5L]}");
  check(value.type == nbt::Type::Compound);
  const auto &children = std::get<nbt::Tag::Container>(value.payload);
  check(children.size() == 7);
  check(children[0].type == nbt::Type::String);
  check(children[1].type == nbt::Type::Short);
  check(children[2].type == nbt::Type::Int);
  check(children[3].type == nbt::Type::List);
  check(children[4].type == nbt::Type::ByteArray);
  check(children[5].type == nbt::Type::IntArray);
  check(children[6].type == nbt::Type::LongArray);
  check(std::get<nbt::Tag::ByteArray>(children[4].payload).size() == 2);
  check(std::get<nbt::Tag::IntArray>(children[5].payload).size() == 2);
  check(std::get<nbt::Tag::LongArray>(children[6].payload).size() == 1);

  const auto compact = U::toSnbt(value);
  const auto pretty = U::toSnbt(value, true);
  check(!compact.empty());
  check(pretty.find('\n') != std::string::npos);
  check(U::parseSnbt(compact).type == nbt::Type::Compound);
}

void testSnbtErrorsAndLimits() {
  using U = nbt::NbtUtilities;

  const auto expectError = [](std::string_view input) {
    bool rejected = false;
    try {
      (void)U::parseSnbt(input);
    } catch (const nbt::Error &) {
      rejected = true;
    }
    check(rejected);
  };

  expectError("{name:\"\\q\"}");
  expectError("1 2");
  expectError("[1,2s]");
  expectError("[B;not-a-number]");

  nbt::Options depthLimit;
  depthLimit.maxDepth = 0;
  bool depthRejected = false;
  try {
    (void)U::parseSnbt("{nested:{value:1}}", depthLimit);
  } catch (const nbt::Error &) {
    depthRejected = true;
  }
  check(depthRejected);

  nbt::Options elementLimit;
  elementLimit.maxContainerElements = 1;
  bool elementRejected = false;
  try {
    (void)U::parseSnbt("{first:1,second:2}", elementLimit);
  } catch (const nbt::Error &) {
    elementRejected = true;
  }
  check(elementRejected);
}

void testSnbtRoundTrip() {
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;

  const auto snbt = U::toSnbt(sample());
  auto decoded = U::parseSnbt(snbt);
  decoded.name = "root";
  check(equalBytes(N(decoded).encode(), N(sample()).encode()));
}

void testCompressedFileRoundTrip() {
  using N = nbt::Nbt;
  using U = nbt::NbtUtilities;

  const auto path = std::filesystem::temp_directory_path() / "nbt-cpp-lazy-test.dat";
  N document(sample());
  for (const auto compression : {U::Compression::None, U::Compression::Gzip, U::Compression::Zlib}) {
    U::save(path, document, compression);
    auto loaded = U::load(path, compression);
    auto autoLoaded = U::load(path);
    check(loaded.root().find("answer").as<nbt::Type::Int>() == 42);
    check(autoLoaded.root().find("name").as<nbt::Type::String>() == "Alex");
  }

  bool autoSaveRejected = false;
  try {
    U::save(path, document, U::Compression::Auto);
  } catch (const std::invalid_argument &) {
    autoSaveRejected = true;
  }
  check(autoSaveRejected);

  bool missingFileRejected = false;
  try {
    (void)U::load(path.string() + ".missing");
  } catch (const std::runtime_error &) {
    missingFileRejected = true;
  }
  check(missingFileRejected);

  U::save(path, document, U::Compression::None);
  nbt::Options inputLimit;
  inputLimit.maxInputBytes = document.encode().size() - 1;
  bool inputLimitRejected = false;
  try {
    (void)U::load(path, U::Compression::None, inputLimit);
  } catch (const nbt::Error &) {
    inputLimitRejected = true;
  }
  check(inputLimitRejected);
  std::filesystem::remove(path);
}

} // namespace

int main() {
  try {
    testSnbtValuesAndFormatting();
    testSnbtErrorsAndLimits();
    testSnbtRoundTrip();
    testCompressedFileRoundTrip();
  } catch (...) {
    return 1;
  }
  return 0;
}
