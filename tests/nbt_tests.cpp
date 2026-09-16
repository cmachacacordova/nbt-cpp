#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "nbt/nbt.h"

#include "zstr.hpp"

namespace {

nbt::Tag makeSampleRoot() {
  using namespace nbt;
  return compoundTag("root",
                     {byteTag("byte", -7),
                      shortTag("short", -300),
                      intTag("int", 123456),
                      longTag("long", INT64_C(0x1020304050607080)),
                      floatTag("float", 1.25f),
                      doubleTag("double", -4.5),
                      stringTag("text", "hello"),
                      byteArrayTag("bytes", {-1, 0, 1}),
                      intArrayTag("ints", {-1, 2}),
                      longArrayTag("longs", {-3, 4}),
                      listTag("list", Type::Int, {intTag("", 1), intTag("", 2)}),
                      compoundTag("nested", {stringTag("value", "ok")})});
}

std::string serialize(const nbt::Tag &root, nbt::BinaryFormat format = nbt::BinaryFormat::File) {
  std::ostringstream output(std::ios::binary);
  nbt::serialize(output, root, format);
  return output.str();
}

nbt::Tag parse(const std::string &data, const nbt::ParseOptions &options = {}) {
  std::istringstream input(data, std::ios::binary);
  return nbt::parse(input, options);
}

void testBinaryStreams() {
  using namespace nbt;
  const auto root = makeSampleRoot();
  const auto bytes = serialize(root);
  assert(equivalent(root, parse(bytes)));

  std::istringstream tokenInput(bytes, std::ios::binary);
  const auto document = tokenize(tokenInput);
  assert(!document.tokens.empty());
  assert(document.tokens.front().type == Type::Compound);
  assert(encodedSize(root) == bytes.size());
  assert(encodedSize(document) == bytes.size());
  static_assert(sizeof(Token) <= 24);
}

void testNetworkStreams() {
  using namespace nbt;
  auto root = makeSampleRoot();
  const auto bytes = serialize(root, BinaryFormat::Network);
  ParseOptions options;
  options.format = BinaryFormat::Network;
  root.name.clear();
  assert(encodedSize(root, BinaryFormat::Network) == bytes.size());
  assert(equivalent(root, parse(bytes, options)));
  assert(static_cast<unsigned char>(bytes.front()) == 0x0a);
}

void testIncrementalParsing() {
  using namespace nbt;
  const auto bytes = serialize(makeSampleRoot());
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
  stream.seekg(0);
  assert(!tryParse(stream));
  assert(stream.tellg() == std::streampos{0});
  stream.clear();
  stream.seekp(0, std::ios::end);
  stream.write(bytes.data() + bytes.size() / 2, static_cast<std::streamsize>(bytes.size() - bytes.size() / 2));
  stream.seekg(0);
  const auto result = tryParse(stream);
  assert(result && equivalent(*result, makeSampleRoot()));
}

void testSnbtAndUtilities() {
  using namespace nbt;
  const auto root = makeSampleRoot();
  assert(equivalent(root, parseSnbt(toSnbt(root, false))));
  assert(size(root) == 16);
  assert(findByPath(root, "root.nested.value")->as<std::string>() == "ok");

  Builder builder("built");
  builder.add(intTag("answer", 42)).beginList("items", Type::String).add(stringTag("", "a")).end();
  assert(equivalent(builder.build(), parse(serialize(builder.build()))));
}

void testFiles() {
  using namespace nbt;
  const auto temporary = std::filesystem::temp_directory_path() / "nbt-cpp-test.dat";
  {
    zstr::ofstream output(temporary.string(), std::ios::binary);
    serialize(output, makeSampleRoot());
  }
  {
    zstr::ifstream input(temporary.string(), std::ios::binary);
    assert(equivalent(nbt::parse(input), makeSampleRoot()));
  }
  save(temporary, makeSampleRoot());
  assert(equivalent(load(temporary, Compression::None), makeSampleRoot()));
  save(temporary, makeSampleRoot(), Compression::Gzip);
  assert(equivalent(load(temporary), makeSampleRoot()));
  save(temporary, makeSampleRoot(), Compression::Zlib);
  assert(equivalent(load(temporary), makeSampleRoot()));
  std::filesystem::remove(temporary);

  const auto levelPath = std::filesystem::path(NBT_CPP_TEST_DATA_DIR) / "level.dat";
  zstr::ifstream input(levelPath.string(), std::ios::binary);
  assert(nbt::parse(input).type == Type::Compound);
}

void testErrors() {
  using namespace nbt;
  auto bytes = serialize(makeSampleRoot());
  bytes.pop_back();
  bool incomplete = false;
  try {
    (void)parse(bytes);
  } catch (const IncompleteDataError &) {
    incomplete = true;
  }
  assert(incomplete);
}

} // namespace

int main() {
  testBinaryStreams();
  testNetworkStreams();
  testIncrementalParsing();
  testSnbtAndUtilities();
  testFiles();
  testErrors();
  std::cout << "nbt-cpp tests passed\n";
}
