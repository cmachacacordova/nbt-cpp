#include "nbt/nbt.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>

int main() {
  using namespace nbt;
  Tag root = Tag::compound("root", {Tag::byte("byte", -7), Tag::shortTag("short", -300), Tag::intTag("int", 123456), Tag::longTag("long", INT64_C(0x1020304050607080)), Tag::floatTag("float", 1.25f), Tag::doubleTag("double", -4.5),
                                    Tag::string("text", "hello"), Tag::byteArray("bytes", {-1, 0, 1}), Tag::intArray("ints", {-1, 2}), Tag::longArray("longs", {-3, 4}), Tag::list("list", Type::Int, {Tag::intTag("", 1), Tag::intTag("", 2)}),
                                    Tag::compound("nested", {Tag::string("value", "ok")})});

  const auto bytes = serialize(root);
  const auto tokens = tokenize(bytes);
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
  ParseOptions network_options;
  network_options.format = BinaryFormat::Network;
  const auto network_bytes = serialize(root, BinaryFormat::Network);
  Tag network_root = root;
  network_root.name.clear();
  assert(equivalent(network_root, parse(network_bytes, network_options)));
  assert(!tokenize(network_bytes, network_options).tokens.empty());
  assert(network_bytes.front() == std::byte{0x0a});
  assert(network_bytes[1] == std::byte{0x01});
  bool rejected_network_scalar = false;
  try {
    const auto invalid = serialize(Tag::intTag("", 1), BinaryFormat::Network);
    (void)invalid;
  } catch (const std::invalid_argument &) {
    rejected_network_scalar = true;
  }
  assert(rejected_network_scalar);
  assert(size(root) == 16);
  assert(findByName(root, "value")->as<std::string>() == "ok");
  assert(findByPath(root, "root.nested.value")->as<std::string>() == "ok");
  assert(at(*findByName(root, "list"), 1)->as<std::int32_t>() == 2);
  const auto snbt = toSnbt(root, false);
  assert(equivalent(root, parseSnbt(snbt)));
  const auto parsed_snbt = parseSnbt("{enabled:true,bytes:[B;1b,-2b],ints:[I;1,-2],longs:[L;1L,-2L]"
                                     ",items:[\"a\",\"b\"]}");
  assert(findByName(parsed_snbt, "enabled")->as<Byte>() == 1);

  std::vector<Token> token_storage(tokens.tokens.size());
  const auto token_view = tokenize(bytes, token_storage);
  assert(token_view.tokens.size() == tokens.tokens.size());
  assert(std::equal(token_view.tokens.begin(), token_view.tokens.end(), tokens.tokens.begin()));
  static_assert(sizeof(Token) <= 24);

  ParseOptions content_options;
  content_options.sourceValidation = SourceValidation::Content;
  const auto content_tokens = tokenize(bytes, content_options);
  const Buffer copied_bytes = bytes;
  bool rejected_copy_identity = false;
  try {
    const auto invalid = parse(copied_bytes, tokens);
    (void)invalid;
  } catch (const Error &) {
    rejected_copy_identity = true;
  }
  assert(rejected_copy_identity);
  assert(equivalent(root, parse(copied_bytes, content_tokens, content_options)));

  Buffer other_bytes = bytes;
  const auto byte_payload = std::find_if(tokens.tokens.begin(), tokens.tokens.end(), [](const Token &token) { return token.kind == TokenKind::Payload && token.type == Type::Byte; });
  assert(byte_payload != tokens.tokens.end());
  other_bytes[byte_payload->begin] ^= std::byte{0x01};
  bool rejected_other_buffer = false;
  try {
    const auto invalid = parse(other_bytes, tokens);
    (void)invalid;
  } catch (const Error &) {
    rejected_other_buffer = true;
  }
  assert(rejected_other_buffer);
  ParseOptions unchecked_options;
  unchecked_options.sourceValidation = SourceValidation::None;
  assert(parse(other_bytes, tokens, unchecked_options).as<Compound>().values.front().as<Byte>() != root.as<Compound>().values.front().as<Byte>());

  for (Compression compression : {Compression::Gzip, Compression::Zlib}) {
    const auto packed = compress(bytes, compression);
    assert(decompress(packed, Compression::Auto) == bytes);
  }

  const std::filesystem::path levelPath = std::filesystem::path(NBT_CPP_TEST_DATA_DIR) / "level.dat";
  const auto gzipLevel = load(levelPath, Compression::Gzip);
  const auto detectedLevel = load(levelPath, Compression::Auto);
  assert(gzipLevel.type == Type::Compound);
  assert(size(gzipLevel) > 1);
  assert(equivalent(gzipLevel, detectedLevel));
  std::cout << "Loaded gzip level.dat (" << size(gzipLevel) << " tags)\n" << toSnbt(gzipLevel, true) << '\n';

  Builder builder("built");
  builder.add(Tag::intTag("answer", 42)).beginList("items", Type::String).add(Tag::string("ignored", "a")).add(Tag::string("", "b")).end();
  const auto built = builder.build();
  assert(equivalent(built, parse(serialize(built))));

  bool threw = false;
  try {
    const auto invalid = parse(std::span<const std::byte>(bytes.data(), bytes.size() - 1));
    (void)invalid;
  } catch (const Error &) {
    threw = true;
  }
  assert(threw);

  std::cout << "nbt-cpp tests passed\n";
}
