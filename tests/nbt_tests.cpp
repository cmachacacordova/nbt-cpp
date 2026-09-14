#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#include "nbt/nbt.h"

namespace {

nbt::Tag makeSampleRoot() {
  using namespace nbt;
  return Tag::compound("root",
                       {Tag::byte("byte", -7),
                        Tag::shortTag("short", -300),
                        Tag::intTag("int", 123456),
                        Tag::longTag("long", INT64_C(0x1020304050607080)),
                        Tag::floatTag("float", 1.25f),
                        Tag::doubleTag("double", -4.5),
                        Tag::string("text", "hello"),
                        Tag::byteArray("bytes", {-1, 0, 1}),
                        Tag::intArray("ints", {-1, 2}),
                        Tag::longArray("longs", {-3, 4}),
                        Tag::list("list", Type::Int, {Tag::intTag("", 1), Tag::intTag("", 2)}),
                        Tag::compound("nested", {Tag::string("value", "ok")})});
}

void testBufferBasics() {
  using namespace nbt;

  Buffer empty;
  assert(empty.empty());
  assert(empty.size() == 0);
  assert(empty.head() == nullptr);
  assert(empty.contiguous().empty());

  Buffer sized(5, std::byte{0xAB});
  assert(sized.size() == 5);
  assert(std::to_integer<unsigned>(sized.front()) == 0xAB);
  assert(std::to_integer<unsigned>(sized[4]) == 0xAB);

  const std::vector<std::byte> raw{std::byte{1}, std::byte{2}, std::byte{3}};
  Buffer copied(raw);
  assert(copied.size() == 3);
  assert(std::to_integer<int>(copied[0]) == 1);

  Buffer moved(std::vector<std::byte>{std::byte{4}, std::byte{5}});
  assert(moved.size() == 2);
  assert(std::to_integer<int>(moved[0]) == 4);

  Buffer view(std::span<const std::byte>{raw});
  assert(view == copied);

  Buffer chain;
  chain.append(std::span<const std::byte>{raw.data(), 1}).append(std::span<const std::byte>{raw.data() + 1, 1}).append(std::span<const std::byte>{raw.data() + 2, 1});
  assert(chain.size() == 3);
  assert(!chain.isContiguous());
  assert(chain == copied);
  assert(chain.flatten() == copied);

  Buffer copy = chain;
  assert(copy == chain);

  Buffer appended;
  appended.append(copied);
  appended.append(chain);
  assert(appended.size() == 6);

  const std::uint8_t rawBytes[] = {0x10, 0x20, 0x30};
  Buffer fromBytes(rawBytes, sizeof(rawBytes));
  assert(fromBytes.size() == 3);
  assert(std::to_integer<unsigned>(fromBytes[2]) == 0x30);
}

void testBufferChainParsing() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto bytes = serialize(root);

  Buffer chain;
  chain.append(std::span<const std::byte>{bytes.data(), 5}).append(std::span<const std::byte>{bytes.data() + 5, bytes.size() - 10}).append(std::span<const std::byte>{bytes.data() + bytes.size() - 5, 5});
  assert(equivalent(root, parse(chain)));

  Buffer emptyChunks;
  emptyChunks.append(std::span<const std::byte>{bytes.data(), 3}).append(std::span<const std::byte>{bytes.data() + 3, std::size_t{0}}).append(std::span<const std::byte>{bytes.data() + 3, 1});
  assert(emptyChunks.size() == 4);
}

void testBufferChainCompression() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto bytes = serialize(root);

  for (Compression compression : {Compression::Gzip, Compression::Zlib}) {
    const auto packed = compress(bytes, compression);
    assert(decompress(packed, Compression::Auto) == bytes);

    Buffer chain;
    for (std::size_t i = 0; i < packed.size(); ++i) {
      chain.append(std::span<const std::byte>{packed.data() + i, 1});
    }
    assert(chain.size() == packed.size());
    assert(decompress(chain, compression) == bytes);
    assert(decompress(chain, Compression::Auto) == bytes);
  }

  assert(decompress(compress(bytes, Compression::None), Compression::None) == bytes);

  Buffer empty;
  assert(compress(empty, Compression::None).empty());
}

void testNetworkBufferChain() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto networkBytes = serialize(root, BinaryFormat::Network);

  Buffer chain;
  chain.append(std::span<const std::byte>{networkBytes.data(), 1}).append(std::span<const std::byte>{networkBytes.data() + 1, networkBytes.size() - 1});

  ParseOptions networkOptions;
  networkOptions.format = BinaryFormat::Network;
  Tag networkRoot = root;
  networkRoot.name.clear();
  assert(equivalent(networkRoot, parse(chain, networkOptions)));
}

void testTokenizationAndViews() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto bytes = serialize(root);
  const auto tokens = tokenize(bytes.contiguous());

  assert(tokens.tokens.front().type == Type::Compound);
  assert(equivalent(root, parse(bytes)));
  assert(equivalent(root, parse(tokens)));
  assert(equivalent(root, parse(bytes, tokens)));

  assert(tokens.get<Type::Short>("short").value() == -300);
  assert(tokens.get<Type::String>("text").value() == "hello");
  assert(tokens.get<Type::ByteArray>("bytes").value()[2] == 1);
  assert(tokens.get<Type::IntArray>("ints").value()[1] == 2);
  assert(tokens.get<Type::LongArray>("longs").value()[0] == -3);
  assert(tokens.get<Type::List>("list").at<Type::Int>(1).value() == 2);
  assert(tokens.get<Type::Compound>("nested").get<Type::String>("value").value() == "ok");
  assert(tokens.getPath<Type::String>("root.nested.value").value() == "ok");
  assert(!tokens.find<Type::Int>("missing"));

  std::vector<Token> storage(tokens.tokens.size());
  const auto view = tokenize(bytes.contiguous(), storage);
  assert(view.tokens.size() == tokens.tokens.size());
  assert(std::equal(view.tokens.begin(), view.tokens.end(), tokens.tokens.begin()));
  static_assert(sizeof(Token) <= 24);
}

void testSourceValidation() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto bytes = serialize(root);
  const auto tokens = tokenize(bytes.contiguous());

  const Buffer copied = bytes;
  bool rejectedIdentity = false;
  try {
    (void)parse(copied, tokens);
  } catch (const Error &) {
    rejectedIdentity = true;
  }
  assert(rejectedIdentity);

  ParseOptions contentOptions;
  contentOptions.sourceValidation = SourceValidation::Content;
  const auto contentTokens = tokenize(bytes.contiguous(), contentOptions);
  assert(equivalent(root, parse(copied, contentTokens, contentOptions)));

  Buffer mutated = bytes;
  const auto payload = std::find_if(tokens.tokens.begin(), tokens.tokens.end(), [](const Token &token) {
    return token.kind == TokenKind::Payload && token.type == Type::Byte;
  });
  assert(payload != tokens.tokens.end());
  mutated[payload->begin] ^= std::byte{0x01};

  bool rejectedMutated = false;
  try {
    (void)parse(mutated, tokens);
  } catch (const Error &) {
    rejectedMutated = true;
  }
  assert(rejectedMutated);

  ParseOptions uncheckedOptions;
  uncheckedOptions.sourceValidation = SourceValidation::None;
  const auto uncheckedRoot = parse(mutated, tokens, uncheckedOptions);
  assert(uncheckedRoot.as<Compound>().values.front().as<Byte>() != root.as<Compound>().values.front().as<Byte>());
}

void testSnbt() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto snbt = toSnbt(root, false);
  assert(equivalent(root, parseSnbt(snbt)));

  const auto parsed = parseSnbt("{enabled:true,bytes:[B;1b,-2b],ints:[I;1,-2],longs:[L;1L,-2L],items:[\"a\",\"b\"]}");
  assert(findByName(parsed, "enabled")->as<Byte>() == 1);
}

void testBuilderAndFileRoundTrip() {
  using namespace nbt;
  Builder builder("built");
  builder.add(Tag::intTag("answer", 42)).beginList("items", Type::String).add(Tag::string("ignored", "a")).add(Tag::string("", "b")).end();
  const auto built = builder.build();
  assert(equivalent(built, parse(serialize(built))));

  const auto tmp = std::filesystem::temp_directory_path() / "nbt-cpp-test.dat";
  save(tmp, built, Compression::Gzip);
  const auto loaded = load(tmp, Compression::Gzip);
  assert(equivalent(built, loaded));
  std::filesystem::remove(tmp);
}

void testLevelDat() {
  using namespace nbt;
  const std::filesystem::path levelPath = std::filesystem::path(NBT_CPP_TEST_DATA_DIR) / "level.dat";
  const auto gzipLevel = load(levelPath, Compression::Gzip);
  const auto detectedLevel = load(levelPath, Compression::Auto);
  assert(gzipLevel.type == Type::Compound);
  assert(size(gzipLevel) > 1);
  assert(equivalent(gzipLevel, detectedLevel));
  std::cout << "Loaded gzip level.dat (" << size(gzipLevel) << " tags)\n";
}

void testNetworkSemantics() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto networkBytes = serialize(root, BinaryFormat::Network);

  ParseOptions networkOptions;
  networkOptions.format = BinaryFormat::Network;
  Tag networkRoot = root;
  networkRoot.name.clear();
  assert(equivalent(networkRoot, parse(networkBytes, networkOptions)));
  assert(!tokenize(networkBytes.contiguous(), networkOptions).tokens.empty());
  assert(networkBytes.front() == std::byte{0x0a});
  assert(networkBytes[1] == std::byte{0x01});

  bool rejected = false;
  try {
    (void)serialize(Tag::intTag("", 1), BinaryFormat::Network);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void testErrorHandling() {
  using namespace nbt;
  const Tag root = makeSampleRoot();
  const auto bytes = serialize(root);

  bool truncated = false;
  try {
    (void)parse(std::span<const std::byte>(bytes.data(), bytes.size() - 1));
  } catch (const Error &) {
    truncated = true;
  }
  assert(truncated);

  bool badGzip = false;
  try {
    Buffer bad;
    bad.append(std::span<const std::byte>{bytes.data(), bytes.size()});
    (void)decompress(bad, Compression::Gzip);
  } catch (const std::runtime_error &) {
    badGzip = true;
  }
  assert(badGzip);
}

} // namespace

int main() {
  using namespace nbt;
  assert(size(makeSampleRoot()) == 16);
  assert(findByName(makeSampleRoot(), "value")->as<std::string>() == "ok");
  assert(findByPath(makeSampleRoot(), "root.nested.value")->as<std::string>() == "ok");
  assert(at(*findByName(makeSampleRoot(), "list"), 1)->as<std::int32_t>() == 2);

  testBufferBasics();
  testBufferChainParsing();
  testBufferChainCompression();
  testNetworkBufferChain();
  testTokenizationAndViews();
  testSourceValidation();
  testSnbt();
  testBuilderAndFileRoundTrip();
  testLevelDat();
  testNetworkSemantics();
  testErrorHandling();

  std::cout << "nbt-cpp tests passed\n";
}
