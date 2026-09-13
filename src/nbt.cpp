#include "nbt/nbt.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <zlib.h>

namespace nbt {
namespace {

std::uint64_t bufferFingerprint(std::span<const std::byte> data) noexcept {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const auto byte : data) {
    hash ^= std::to_integer<std::uint8_t>(byte);
    hash *= UINT64_C(1099511628211);
  }
  hash ^= data.size();
  hash *= UINT64_C(1099511628211);
  return hash;
}

class Reader {
public:
  Reader(std::span<const std::byte> data, const ParseOptions &options, std::vector<Token> *tokens) : data_(data), options_(options), tokens_(tokens) {}

  Tag root() {
    Tag result;
    if (options_.format == BinaryFormat::File)
      result = named(std::nullopt, 0);
    else {
      const auto begin = pos_;
      const Type rootType = type();
      if (rootType != Type::Compound)
        fail("Network NBT root must be TAG_Compound");
      const auto token = push(TokenKind::Tag, rootType, begin, std::nullopt);
      result = payload(rootType, {}, token, 0);
      finish(token, pos_);
    }
    if (options_.requireCompleteInput && pos_ != data_.size())
      fail("trailing data");
    return result;
  }

private:
  template <class T> T number() {
    static_assert(std::is_arithmetic_v<T>);
    require(sizeof(T));
    using U = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    U bits{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
      bits = static_cast<U>((bits << 8) | std::to_integer<std::uint8_t>(data_[pos_++]));
    if constexpr (std::is_floating_point_v<T>)
      return std::bit_cast<T>(bits);
    else
      return static_cast<T>(bits);
  }

  std::string string() {
    const auto length = number<std::uint16_t>();
    require(length);
    std::string result(length, '\0');
    for (std::size_t i = 0; i < length; ++i)
      result[i] = static_cast<char>(data_[pos_ + i]);
    pos_ += length;
    return result;
  }

  std::size_t count() {
    const auto value = number<std::int32_t>();
    if (value < 0)
      fail("negative array or list length");
    if (static_cast<std::size_t>(value) > options_.maxElements)
      fail("element limit exceeded");
    return static_cast<std::size_t>(value);
  }

  Type type() {
    const auto value = number<std::uint8_t>();
    if (value > static_cast<unsigned>(Type::LongArray))
      fail("unknown tag type");
    return static_cast<Type>(value);
  }

  Tag named(std::optional<std::size_t> parent, std::size_t depth) {
    const auto begin = pos_;
    const Type tagType = type();
    if (tagType == Type::End)
      fail("TAG_End cannot be a named tag");
    const auto token = push(TokenKind::Tag, tagType, begin, parent);
    const auto nameBegin = pos_;
    std::string name = string();
    pushDone(TokenKind::Name, tagType, nameBegin, pos_, token);
    Tag result = payload(tagType, std::move(name), token, depth);
    finish(token, pos_);
    return result;
  }

  Tag payload(Type tagType, std::string name, std::optional<std::size_t> parent, std::size_t depth) {
    if (depth > options_.maxDepth)
      fail("depth limit exceeded");
    const auto begin = pos_;
    Tag result;
    switch (tagType) {
    case Type::Byte:
      result = Tag::byte(std::move(name), number<Byte>());
      break;
    case Type::Short:
      result = Tag::shortTag(std::move(name), number<std::int16_t>());
      break;
    case Type::Int:
      result = Tag::intTag(std::move(name), number<std::int32_t>());
      break;
    case Type::Long:
      result = Tag::longTag(std::move(name), number<std::int64_t>());
      break;
    case Type::Float:
      result = Tag::floatTag(std::move(name), number<float>());
      break;
    case Type::Double:
      result = Tag::doubleTag(std::move(name), number<double>());
      break;
    case Type::ByteArray: {
      const auto n = count();
      require(n);
      ByteArray values(n);
      for (auto &value : values)
        value = number<Byte>();
      result = Tag::byteArray(std::move(name), std::move(values));
      break;
    }
    case Type::String:
      result = Tag::string(std::move(name), string());
      break;
    case Type::IntArray: {
      const auto n = count();
      IntArray values(n);
      if (n > remaining() / 4)
        fail("truncated int array");
      for (auto &value : values)
        value = number<std::int32_t>();
      result = Tag::intArray(std::move(name), std::move(values));
      break;
    }
    case Type::LongArray: {
      const auto n = count();
      LongArray values(n);
      if (n > remaining() / 8)
        fail("truncated long array");
      for (auto &value : values)
        value = number<std::int64_t>();
      result = Tag::longArray(std::move(name), std::move(values));
      break;
    }
    case Type::List: {
      const Type element = type();
      const auto n = count();
      if (element == Type::End && n != 0)
        fail("non-empty TAG_List has TAG_End element type");
      std::vector<Tag> values;
      values.reserve(n);
      for (std::size_t i = 0; i < n; ++i) {
        const auto childBegin = pos_;
        const auto child = push(TokenKind::Tag, element, childBegin, parent);
        values.push_back(payload(element, {}, child, depth + 1));
        finish(child, pos_);
      }
      result = Tag::list(std::move(name), element, std::move(values));
      if (parent && tokens_) {
        (*tokens_)[*parent].count = static_cast<std::uint32_t>(n);
        (*tokens_)[*parent].elementType = element;
      }
      break;
    }
    case Type::Compound: {
      std::vector<Tag> values;
      while (true) {
        require(1);
        if (std::to_integer<std::uint8_t>(data_[pos_]) == 0) {
          ++pos_;
          break;
        }
        if (values.size() >= options_.maxElements)
          fail("element limit exceeded");
        values.push_back(named(parent, depth + 1));
      }
      result = Tag::compound(std::move(name), std::move(values));
      break;
    }
    case Type::End:
      fail("unexpected TAG_End");
    }
    pushDone(TokenKind::Payload, tagType, begin, pos_, parent);
    return result;
  }

  std::optional<std::size_t> push(TokenKind kind, Type type, std::size_t begin, std::optional<std::size_t> parent) {
    if (!tokens_)
      return std::nullopt;
    Token token;
    token.kind = kind;
    token.type = type;
    token.begin = static_cast<std::uint32_t>(begin);
    token.end = token.subtreeEnd = token.begin;
    token.parent = parent ? static_cast<std::uint32_t>(*parent) : Token::noParent;
    tokens_->push_back(token);
    return tokens_->size() - 1;
  }
  void pushDone(TokenKind kind, Type type, std::size_t begin, std::size_t end, std::optional<std::size_t> parent) {
    if (tokens_) {
      Token token;
      token.kind = kind;
      token.type = type;
      token.begin = static_cast<std::uint32_t>(begin);
      token.end = token.subtreeEnd = static_cast<std::uint32_t>(end);
      token.parent = parent ? static_cast<std::uint32_t>(*parent) : Token::noParent;
      tokens_->push_back(token);
    }
  }
  void finish(std::optional<std::size_t> index, std::size_t end) {
    if (index && tokens_) {
      (*tokens_)[*index].end = static_cast<std::uint32_t>(end);
      (*tokens_)[*index].subtreeEnd = static_cast<std::uint32_t>(tokens_->size());
    }
  }
  void require(std::size_t n) {
    if (n > remaining())
      fail("truncated NBT data");
  }
  [[noreturn]] void fail(std::string message) const { throw Error(std::move(message), pos_); }
  std::size_t remaining() const { return data_.size() - pos_; }

  std::span<const std::byte> data_;
  const ParseOptions &options_;
  std::vector<Token> *tokens_;
  std::size_t pos_{};
};

class Tokenizer {
public:
  Tokenizer(std::span<const std::byte> data, const ParseOptions &options, std::vector<Token> *dynamic, std::span<Token> fixed) : data_(data), options_(options), dynamic_(dynamic), fixed_(fixed) {
    if (data.size() > UINT32_MAX)
      throw Error("buffers larger than 4 GiB require 64-bit tokens", 0);
  }

  std::size_t run() {
    if (options_.format == BinaryFormat::File)
      named(Token::noParent, 0);
    else {
      const auto begin = pos_;
      const auto tagType = type();
      if (tagType != Type::Compound)
        fail("Network NBT root must be TAG_Compound");
      const auto root = beginToken(TokenKind::Tag, tagType, begin, Token::noParent);
      payload(tagType, root, 0);
      finish(root);
    }
    if (options_.requireCompleteInput && pos_ != data_.size())
      fail("trailing data");
    return used_;
  }

private:
  template <class T> T number() {
    require(sizeof(T));
    std::uint64_t value{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
      value = (value << 8) | std::to_integer<std::uint8_t>(data_[pos_++]);
    return static_cast<T>(value);
  }
  Type type() {
    const auto value = number<std::uint8_t>();
    if (value > static_cast<unsigned>(Type::LongArray))
      fail("unknown tag type");
    return static_cast<Type>(value);
  }
  std::size_t count() {
    const auto value = number<std::int32_t>();
    if (value < 0)
      fail("negative array or list length");
    if (static_cast<std::size_t>(value) > options_.maxElements)
      fail("element limit exceeded");
    return static_cast<std::size_t>(value);
  }
  void string() {
    const auto n = number<std::uint16_t>();
    require(n);
    pos_ += n;
  }
  void named(std::uint32_t parent, std::size_t depth) {
    const auto begin = pos_;
    const auto tagType = type();
    if (tagType == Type::End)
      fail("TAG_End cannot be a named tag");
    const auto tag = beginToken(TokenKind::Tag, tagType, begin, parent);
    const auto nameBegin = pos_;
    string();
    emit(doneToken(TokenKind::Name, tagType, nameBegin, pos_, tag));
    payload(tagType, tag, depth);
    finish(tag);
  }
  void payload(Type tagType, std::uint32_t parent, std::size_t depth) {
    if (depth > options_.maxDepth)
      fail("depth limit exceeded");
    const auto begin = pos_;
    switch (tagType) {
    case Type::Byte:
      skip(1);
      break;
    case Type::Short:
      skip(2);
      break;
    case Type::Int:
    case Type::Float:
      skip(4);
      break;
    case Type::Long:
    case Type::Double:
      skip(8);
      break;
    case Type::String:
      string();
      break;
    case Type::ByteArray: {
      const auto n = count();
      skip(n);
      break;
    }
    case Type::IntArray: {
      const auto n = count();
      if (n > remaining() / 4)
        fail("truncated int array");
      skip(n * 4);
      break;
    }
    case Type::LongArray: {
      const auto n = count();
      if (n > remaining() / 8)
        fail("truncated long array");
      skip(n * 8);
      break;
    }
    case Type::List: {
      const auto element = type();
      const auto n = count();
      if (element == Type::End && n)
        fail("non-empty TAG_List has TAG_End element type");
      token(parent).count = static_cast<std::uint32_t>(n);
      token(parent).elementType = element;
      for (std::size_t i = 0; i < n; ++i) {
        const auto child = beginToken(TokenKind::Tag, element, pos_, parent);
        payload(element, child, depth + 1);
        finish(child);
      }
      break;
    }
    case Type::Compound: {
      std::size_t n{};
      while (true) {
        require(1);
        if (std::to_integer<std::uint8_t>(data_[pos_]) == 0) {
          ++pos_;
          break;
        }
        if (++n > options_.maxElements)
          fail("element limit exceeded");
        named(parent, depth + 1);
      }
      token(parent).count = static_cast<std::uint32_t>(n);
      break;
    }
    case Type::End:
      fail("unexpected TAG_End");
    }
    emit(doneToken(TokenKind::Payload, tagType, begin, pos_, parent));
  }
  Token doneToken(TokenKind kind, Type type, std::size_t begin, std::size_t end, std::uint32_t parent) {
    Token t;
    t.kind = kind;
    t.type = type;
    t.begin = static_cast<std::uint32_t>(begin);
    t.end = t.subtreeEnd = static_cast<std::uint32_t>(end);
    t.parent = parent;
    return t;
  }
  std::uint32_t beginToken(TokenKind kind, Type type, std::size_t begin, std::uint32_t parent) {
    const auto index = used_;
    emit(doneToken(kind, type, begin, begin, parent));
    return static_cast<std::uint32_t>(index);
  }
  void finish(std::uint32_t index) {
    auto &t = token(index);
    t.end = static_cast<std::uint32_t>(pos_);
    t.subtreeEnd = static_cast<std::uint32_t>(used_);
  }
  void emit(Token value) {
    if (dynamic_)
      dynamic_->push_back(value);
    else {
      if (used_ >= fixed_.size())
        fail("token buffer exhausted");
      fixed_[used_] = value;
    }
    ++used_;
  }
  Token &token(std::uint32_t index) { return dynamic_ ? (*dynamic_)[index] : fixed_[index]; }
  void skip(std::size_t n) {
    require(n);
    pos_ += n;
  }
  void require(std::size_t n) {
    if (n > remaining())
      fail("truncated NBT data");
  }
  std::size_t remaining() const { return data_.size() - pos_; }
  [[noreturn]] void fail(std::string message) const { throw Error(std::move(message), pos_); }
  std::span<const std::byte> data_;
  const ParseOptions &options_;
  std::vector<Token> *dynamic_;
  std::span<Token> fixed_;
  std::size_t pos_{};
  std::size_t used_{};
};

class Writer {
public:
  Buffer run(const Tag &root, bool includeName) {
    out_.reserve(encodedSize(root, includeName));
    if (includeName)
      named(root);
    else {
      number<std::uint8_t>(static_cast<std::uint8_t>(root.type));
      payload(root);
    }
    return std::move(out_);
  }

private:
  static std::size_t encodedSize(const Tag &tag, bool named) {
    std::size_t size = named ? 3 + tag.name.size() : 0;
    switch (tag.type) {
    case Type::Byte:
      return size + 1;
    case Type::Short:
      return size + 2;
    case Type::Int:
    case Type::Float:
      return size + 4;
    case Type::Long:
    case Type::Double:
      return size + 8;
    case Type::String:
      return size + 2 + tag.as<std::string>().size();
    case Type::ByteArray:
      return size + 4 + tag.as<ByteArray>().size();
    case Type::IntArray:
      return size + 4 + tag.as<IntArray>().size() * 4;
    case Type::LongArray:
      return size + 4 + tag.as<LongArray>().size() * 8;
    case Type::List: {
      size += 5;
      for (const auto &child : tag.as<List>().values)
        size += encodedSize(child, false);
      return size;
    }
    case Type::Compound: {
      for (const auto &child : tag.as<Compound>().values)
        size += encodedSize(child, true);
      return size + 1;
    }
    case Type::End:
      return size;
    }
    return size;
  }
  template <class T> void number(T value) {
    using U = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    U bits;
    if constexpr (std::is_floating_point_v<T>)
      bits = std::bit_cast<U>(value);
    else
      bits = static_cast<U>(value);
    for (std::size_t i = sizeof(T); i > 0; --i)
      out_.push_back(static_cast<std::byte>((bits >> ((i - 1) * 8)) & 0xff));
  }
  void string(std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint16_t>::max())
      throw std::invalid_argument("NBT string exceeds 65535 bytes");
    number<std::uint16_t>(static_cast<std::uint16_t>(value.size()));
    for (unsigned char byte : value)
      out_.push_back(static_cast<std::byte>(byte));
  }
  void named(const Tag &tag) {
    if (tag.type == Type::End)
      throw std::invalid_argument("TAG_End cannot be serialized as a named tag");
    number<std::uint8_t>(static_cast<std::uint8_t>(tag.type));
    string(tag.name);
    payload(tag);
  }
  void length(std::size_t n) {
    if (n > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
      throw std::invalid_argument("NBT collection is too large");
    number<std::int32_t>(static_cast<std::int32_t>(n));
  }
  void payload(const Tag &tag) {
    switch (tag.type) {
    case Type::Byte:
      number(std::get<Byte>(tag.value));
      break;
    case Type::Short:
      number(std::get<std::int16_t>(tag.value));
      break;
    case Type::Int:
      number(std::get<std::int32_t>(tag.value));
      break;
    case Type::Long:
      number(std::get<std::int64_t>(tag.value));
      break;
    case Type::Float:
      number(std::get<float>(tag.value));
      break;
    case Type::Double:
      number(std::get<double>(tag.value));
      break;
    case Type::ByteArray: {
      const auto &v = std::get<ByteArray>(tag.value);
      length(v.size());
      for (auto x : v)
        number(x);
      break;
    }
    case Type::String:
      string(std::get<std::string>(tag.value));
      break;
    case Type::IntArray: {
      const auto &v = std::get<IntArray>(tag.value);
      length(v.size());
      for (auto x : v)
        number(x);
      break;
    }
    case Type::LongArray: {
      const auto &v = std::get<LongArray>(tag.value);
      length(v.size());
      for (auto x : v)
        number(x);
      break;
    }
    case Type::List: {
      const auto &v = std::get<List>(tag.value).values;
      for (const auto &child : v)
        if (child.type != tag.elementType || !child.name.empty())
          throw std::invalid_argument("invalid heterogeneous or named TAG_List element");
      number<std::uint8_t>(static_cast<std::uint8_t>(tag.elementType));
      length(v.size());
      for (const auto &child : v)
        payload(child);
      break;
    }
    case Type::Compound:
      for (const auto &child : std::get<Compound>(tag.value).values)
        named(child);
      number<std::uint8_t>(0);
      break;
    case Type::End:
      throw std::invalid_argument("unexpected TAG_End");
    }
  }
  Buffer out_;
};

std::vector<Tag> &children(Tag &tag) { return tag.type == Type::List ? tag.as<List>().values : tag.as<Compound>().values; }
const std::vector<Tag> &children(const Tag &tag) { return tag.type == Type::List ? tag.as<List>().values : tag.as<Compound>().values; }
bool container(const Tag &tag) { return tag.type == Type::List || tag.type == Type::Compound; }

std::string escape(std::string_view text) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      out << '\\' << c;
    else if (c == '\n')
      out << "\\n";
    else if (c == '\r')
      out << "\\r";
    else if (c == '\t')
      out << "\\t";
    else if (c < 0x20)
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
    else
      out << c;
  }
  return out.str() + '"';
}

void snbt(const Tag &tag, std::ostringstream &out, bool pretty, std::size_t depth, bool named) {
  if (named && !tag.name.empty())
    out << escape(tag.name) << ':' << (pretty ? " " : "");
  const auto indent = [&](std::size_t d) {
    if (pretty)
      out << '\n' << std::string(d * 2, ' ');
  };
  switch (tag.type) {
  case Type::Byte:
    out << +tag.as<Byte>() << 'b';
    break;
  case Type::Short:
    out << tag.as<std::int16_t>() << 's';
    break;
  case Type::Int:
    out << tag.as<std::int32_t>();
    break;
  case Type::Long:
    out << tag.as<std::int64_t>() << 'L';
    break;
  case Type::Float:
    out << tag.as<float>() << 'f';
    break;
  case Type::Double:
    out << tag.as<double>() << 'd';
    break;
  case Type::String:
    out << escape(tag.as<std::string>());
    break;
  case Type::ByteArray: {
    out << "[B;";
    const auto &v = tag.as<ByteArray>();
    for (size_t i = 0; i < v.size(); ++i) {
      if (i)
        out << ',';
      out << +v[i] << 'b';
    }
    out << ']';
    break;
  }
  case Type::IntArray: {
    out << "[I;";
    const auto &v = tag.as<IntArray>();
    for (size_t i = 0; i < v.size(); ++i) {
      if (i)
        out << ',';
      out << v[i];
    }
    out << ']';
    break;
  }
  case Type::LongArray: {
    out << "[L;";
    const auto &v = tag.as<LongArray>();
    for (size_t i = 0; i < v.size(); ++i) {
      if (i)
        out << ',';
      out << v[i] << 'L';
    }
    out << ']';
    break;
  }
  case Type::List: {
    out << '[';
    const auto &v = tag.as<List>().values;
    for (size_t i = 0; i < v.size(); ++i) {
      if (i)
        out << ',';
      if (pretty)
        indent(depth + 1);
      snbt(v[i], out, pretty, depth + 1, false);
    }
    if (pretty && !v.empty())
      indent(depth);
    out << ']';
    break;
  }
  case Type::Compound: {
    out << '{';
    const auto &v = tag.as<Compound>().values;
    for (size_t i = 0; i < v.size(); ++i) {
      if (i)
        out << ',';
      if (pretty)
        indent(depth + 1);
      snbt(v[i], out, pretty, depth + 1, true);
    }
    if (pretty && !v.empty())
      indent(depth);
    out << '}';
    break;
  }
  case Type::End:
    out << "END";
    break;
  }
}

Buffer zcode(std::span<const std::byte> input, int window_bits, bool encode) {
  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  int status = encode ? deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) : inflateInit2(&stream, window_bits);
  if (status != Z_OK)
    throw std::runtime_error("zlib initialization failed");
  Buffer output;
  std::byte block[4096];
  do {
    stream.next_out = reinterpret_cast<Bytef *>(block);
    stream.avail_out = sizeof block;
    status = encode ? deflate(&stream, Z_FINISH) : inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END && !(encode && status == Z_BUF_ERROR)) {
      if (encode)
        deflateEnd(&stream);
      else
        inflateEnd(&stream);
      throw std::runtime_error("invalid or unsupported compressed NBT data");
    }
    output.insert(output.end(), block, block + sizeof block - stream.avail_out);
  } while (status != Z_STREAM_END);
  if (encode)
    deflateEnd(&stream);
  else
    inflateEnd(&stream);
  return output;
}

class SnbtParser {
public:
  SnbtParser(std::string_view input, const ParseOptions &options) : input_(input), options_(options) {}
  Tag run() {
    space();
    Tag result;
    if (peek() == '\'' || peek() == '"') {
      auto saved = pos_;
      auto name = quoted();
      space();
      if (accept(':'))
        result = value(std::move(name), 0);
      else {
        pos_ = saved;
        result = value({}, 0);
      }
    } else
      result = value({}, 0);
    space();
    if (pos_ != input_.size())
      fail("trailing SNBT data");
    return result;
  }

private:
  Tag value(std::string name, std::size_t depth) {
    if (depth > options_.maxDepth)
      fail("depth limit exceeded");
    space();
    if (pos_ >= input_.size())
      fail("expected SNBT value");
    if (peek() == '{')
      return compound(std::move(name), depth);
    if (peek() == '[')
      return list(std::move(name), depth);
    if (peek() == '\'' || peek() == '"')
      return Tag::string(std::move(name), quoted());
    auto text = bare();
    if (text.empty())
      fail("expected SNBT value");
    return scalar(std::move(name), text);
  }
  Tag compound(std::string name, std::size_t depth) {
    take('{');
    std::vector<Tag> values;
    space();
    if (accept('}'))
      return Tag::compound(std::move(name));
    while (true) {
      if (values.size() >= options_.maxElements)
        fail("element limit exceeded");
      std::string key = (peek() == '\'' || peek() == '"') ? quoted() : bareKey();
      space();
      take(':');
      values.push_back(value(std::move(key), depth + 1));
      space();
      if (accept('}'))
        break;
      take(',');
    }
    return Tag::compound(std::move(name), std::move(values));
  }
  Tag list(std::string name, std::size_t depth) {
    take('[');
    space();
    if (pos_ + 1 < input_.size() && (input_[pos_] == 'B' || input_[pos_] == 'I' || input_[pos_] == 'L') && input_[pos_ + 1] == ';')
      return typedArray(std::move(name));
    std::vector<Tag> values;
    if (accept(']'))
      return Tag::list(std::move(name), Type::End);
    while (true) {
      if (values.size() >= options_.maxElements)
        fail("element limit exceeded");
      values.push_back(value({}, depth + 1));
      if (values.size() > 1 && values.back().type != values.front().type)
        fail("SNBT lists must be homogeneous");
      space();
      if (accept(']'))
        break;
      take(',');
    }
    const auto elementType = values.front().type;
    return Tag::list(std::move(name), elementType, std::move(values));
  }
  Tag typedArray(std::string name) {
    char kind = input_[pos_];
    pos_ += 2;
    space();
    if (kind == 'B') {
      ByteArray out;
      if (accept(']'))
        return Tag::byteArray(std::move(name), {});
      while (true) {
        auto t = scalar({}, bare());
        if (t.type != Type::Byte)
          fail("TAG_Byte_Array requires byte values");
        out.push_back(t.as<Byte>());
        space();
        if (accept(']'))
          break;
        take(',');
      }
      return Tag::byteArray(std::move(name), std::move(out));
    }
    if (kind == 'I') {
      IntArray out;
      if (accept(']'))
        return Tag::intArray(std::move(name), {});
      while (true) {
        auto t = scalar({}, bare());
        if (t.type != Type::Int)
          fail("TAG_Int_Array requires int values");
        out.push_back(t.as<std::int32_t>());
        space();
        if (accept(']'))
          break;
        take(',');
      }
      return Tag::intArray(std::move(name), std::move(out));
    }
    LongArray out;
    if (accept(']'))
      return Tag::longArray(std::move(name), {});
    while (true) {
      auto t = scalar({}, bare());
      if (t.type != Type::Long)
        fail("TAG_Long_Array requires long values");
      out.push_back(t.as<std::int64_t>());
      space();
      if (accept(']'))
        break;
      take(',');
    }
    return Tag::longArray(std::move(name), std::move(out));
  }
  Tag scalar(std::string name, std::string_view text) {
    try {
      if (text == "true")
        return Tag::byte(std::move(name), 1);
      if (text == "false")
        return Tag::byte(std::move(name), 0);
      char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
      auto body = text;
      if (suffix == 'b' || suffix == 's' || suffix == 'l' || suffix == 'f' || suffix == 'd')
        body.remove_suffix(1);
      std::string copy(body);
      std::size_t used{};
      if (suffix == 'f')
        return Tag::floatTag(std::move(name), std::stof(copy, &used));
      if (suffix == 'd')
        return Tag::doubleTag(std::move(name), std::stod(copy, &used));
      if (copy.find_first_of(".eE") != std::string::npos)
        return Tag::doubleTag(std::move(name), std::stod(copy, &used));
      auto number = std::stoll(copy, &used, 10);
      if (used != copy.size())
        return Tag::string(std::move(name), std::string(text));
      if (suffix == 'b') {
        if (number < INT8_MIN || number > INT8_MAX)
          fail("byte out of range");
        return Tag::byte(std::move(name), static_cast<Byte>(number));
      }
      if (suffix == 's') {
        if (number < INT16_MIN || number > INT16_MAX)
          fail("short out of range");
        return Tag::shortTag(std::move(name), static_cast<std::int16_t>(number));
      }
      if (suffix == 'l')
        return Tag::longTag(std::move(name), number);
      if (number < INT32_MIN || number > INT32_MAX)
        fail("int out of range");
      return Tag::intTag(std::move(name), static_cast<std::int32_t>(number));
    } catch (const std::invalid_argument &) {
      return Tag::string(std::move(name), std::string(text));
    } catch (const std::out_of_range &) {
      fail("numeric value out of range");
    }
  }
  std::string quoted() {
    char quote = input_[pos_++];
    std::string out;
    while (pos_ < input_.size()) {
      char c = input_[pos_++];
      if (c == quote)
        return out;
      if (c == '\\') {
        if (pos_ >= input_.size())
          fail("unterminated escape");
        char e = input_[pos_++];
        if (e == quote || e == '\\')
          out.push_back(e);
        else
          fail("invalid SNBT escape");
      } else
        out.push_back(c);
    }
    fail("unterminated string");
  }
  std::string_view bare() {
    space();
    auto start = pos_;
    while (pos_ < input_.size() && input_[pos_] != ',' && input_[pos_] != ']' && input_[pos_] != '}' && !std::isspace(static_cast<unsigned char>(input_[pos_])))
      ++pos_;
    return input_.substr(start, pos_ - start);
  }
  std::string bareKey() {
    space();
    auto start = pos_;
    while (pos_ < input_.size() && input_[pos_] != ':' && !std::isspace(static_cast<unsigned char>(input_[pos_])))
      ++pos_;
    if (start == pos_)
      fail("expected compound key");
    return std::string(input_.substr(start, pos_ - start));
  }
  void space() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])))
      ++pos_;
  }
  char peek() {
    space();
    return pos_ < input_.size() ? input_[pos_] : '\0';
  }
  bool accept(char c) {
    space();
    if (pos_ < input_.size() && input_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  void take(char c) {
    if (!accept(c))
      fail(std::string("expected '") + c + "'");
  }
  [[noreturn]] void fail(std::string message) const { throw Error(std::move(message), pos_); }
  std::string_view input_;
  const ParseOptions &options_;
  std::size_t pos_{};
};

} // namespace

Error::Error(std::string message, std::size_t offset) : std::runtime_error(std::move(message)), offset_(offset) {}
std::size_t Error::offset() const noexcept { return offset_; }

namespace detail {
std::uint64_t readUnsigned(std::span<const std::byte> source, std::uint32_t begin, std::size_t size) {
  if (size > 8 || begin > source.size() || size > source.size() - begin)
    throw Error("NBT view is outside the source buffer", begin);
  std::uint64_t value{};
  for (std::size_t i = 0; i < size; ++i)
    value = (value << 8) | std::to_integer<std::uint8_t>(source[begin + i]);
  return value;
}

std::string_view name(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t tag) noexcept {
  if (tag >= tokens.size())
    return {};
  const auto limit = std::min<std::size_t>(tokens[tag].subtreeEnd, tokens.size());
  for (std::size_t i = tag + 1; i < limit; ++i) {
    const auto &entry = tokens[i];
    if (entry.kind != TokenKind::Name || entry.parent != tag)
      continue;
    if (entry.end - entry.begin < 2 || entry.end > source.size())
      return {};
    const auto length = static_cast<std::size_t>((std::to_integer<std::uint8_t>(source[entry.begin]) << 8) | std::to_integer<std::uint8_t>(source[entry.begin + 1]));
    if (length > entry.end - entry.begin - 2)
      return {};
    return {reinterpret_cast<const char *>(source.data() + entry.begin + 2), length};
  }
  return {};
}

std::optional<std::uint32_t> findChild(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view expected) noexcept {
  if (parent >= tokens.size())
    return std::nullopt;
  const auto limit = std::min<std::size_t>(tokens[parent].subtreeEnd, tokens.size());
  for (std::size_t i = parent + 1; i < limit; ++i) {
    const auto &entry = tokens[i];
    if (entry.kind != TokenKind::Tag)
      continue;
    if (entry.parent == parent && name(source, tokens, static_cast<std::uint32_t>(i)) == expected)
      return static_cast<std::uint32_t>(i);
    if (entry.subtreeEnd > i)
      i = entry.subtreeEnd - 1;
  }
  return std::nullopt;
}

std::optional<std::uint32_t> findPath(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view path) noexcept {
  if (parent >= tokens.size())
    return std::nullopt;
  const auto firstEnd = path.find('.');
  const auto first = path.substr(0, firstEnd);
  if (first == name(source, tokens, parent)) {
    if (firstEnd == std::string_view::npos)
      return parent;
    path.remove_prefix(firstEnd + 1);
  }
  while (!path.empty()) {
    const auto end = path.find('.');
    const auto part = path.substr(0, end);
    const auto child = findChild(source, tokens, parent, part);
    if (!child)
      return std::nullopt;
    parent = *child;
    if (end == std::string_view::npos)
      return parent;
    path.remove_prefix(end + 1);
  }
  return parent;
}

std::optional<std::uint32_t> listItem(std::span<const Token> tokens, std::uint32_t parent, std::size_t requested) noexcept {
  if (parent >= tokens.size() || tokens[parent].type != Type::List)
    return std::nullopt;
  const auto limit = std::min<std::size_t>(tokens[parent].subtreeEnd, tokens.size());
  std::size_t current{};
  for (std::size_t i = parent + 1; i < limit; ++i) {
    const auto &entry = tokens[i];
    if (entry.kind != TokenKind::Tag || entry.parent != parent)
      continue;
    if (current++ == requested)
      return static_cast<std::uint32_t>(i);
    if (entry.subtreeEnd > i)
      i = entry.subtreeEnd - 1;
  }
  return std::nullopt;
}

const Token &payload(std::span<const Token> tokens, std::uint32_t tag) {
  if (tag >= tokens.size())
    throw Error("invalid NBT view token", 0);
  const auto limit = std::min<std::size_t>(tokens[tag].subtreeEnd, tokens.size());
  for (std::size_t i = tag + 1; i < limit; ++i)
    if (tokens[i].kind == TokenKind::Payload && tokens[i].parent == tag)
      return tokens[i];
  throw Error("NBT tag has no payload token", tokens[tag].begin);
}
} // namespace detail

std::int32_t IntArrayView::operator[](std::size_t index) const {
  if (index >= size_)
    throw std::out_of_range("NBT int array index");
  return static_cast<std::int32_t>(detail::readUnsigned(source_, begin_ + static_cast<std::uint32_t>(index * 4), 4));
}

std::int64_t LongArrayView::operator[](std::size_t index) const {
  if (index >= size_)
    throw std::out_of_range("NBT long array index");
  return static_cast<std::int64_t>(detail::readUnsigned(source_, begin_ + static_cast<std::uint32_t>(index * 8), 8));
}

Tag::Tag(Type t, std::string n, Value v, Type e) : type(t), name(std::move(n)), value(std::move(v)), elementType(e) {}
Tag Tag::byte(std::string n, Byte v) { return {Type::Byte, std::move(n), v}; }
Tag Tag::shortTag(std::string n, std::int16_t v) { return {Type::Short, std::move(n), v}; }
Tag Tag::intTag(std::string n, std::int32_t v) { return {Type::Int, std::move(n), v}; }
Tag Tag::longTag(std::string n, std::int64_t v) { return {Type::Long, std::move(n), v}; }
Tag Tag::floatTag(std::string n, float v) { return {Type::Float, std::move(n), v}; }
Tag Tag::doubleTag(std::string n, double v) { return {Type::Double, std::move(n), v}; }
Tag Tag::byteArray(std::string n, ByteArray v) { return {Type::ByteArray, std::move(n), std::move(v)}; }
Tag Tag::string(std::string n, std::string v) { return {Type::String, std::move(n), std::move(v)}; }
Tag Tag::list(std::string n, Type e, std::vector<Tag> v) { return {Type::List, std::move(n), List{std::move(v)}, e}; }
Tag Tag::compound(std::string n, std::vector<Tag> v) { return {Type::Compound, std::move(n), Compound{std::move(v)}}; }
Tag Tag::intArray(std::string n, IntArray v) { return {Type::IntArray, std::move(n), std::move(v)}; }
Tag Tag::longArray(std::string n, LongArray v) { return {Type::LongArray, std::move(n), std::move(v)}; }

TokenizedDocument tokenize(std::span<const std::byte> input, const ParseOptions &options) {
  TokenizedDocument document;
  document.source = input;
  document.format = options.format;
  document.tokens.reserve(std::min<std::size_t>(input.size() / 8 + 1, options.maxElements));
  Tokenizer(input, options, &document.tokens, {}).run();
  if (options.sourceValidation == SourceValidation::Content) {
    document.fingerprint = bufferFingerprint(input);
    document.hasFingerprint = true;
  }
  return document;
}
TokenizedView tokenize(std::span<const std::byte> input, std::span<Token> output, const ParseOptions &options) {
  const auto count = Tokenizer(input, options, nullptr, output).run();
  TokenizedView document{input, output.first(count), 0, false, options.format};
  if (options.sourceValidation == SourceValidation::Content) {
    document.fingerprint = bufferFingerprint(input);
    document.hasFingerprint = true;
  }
  return document;
}
Tag parse(std::span<const std::byte> input, const ParseOptions &options) { return Reader(input, options, nullptr).root(); }

namespace {
Tag parseTokenized(std::span<const std::byte> input, std::span<const std::byte> source, std::span<const Token> tokens, std::uint64_t fingerprint, bool hasFingerprint, BinaryFormat format, const ParseOptions &options) {
  if (tokens.empty() || tokens.front().kind != TokenKind::Tag || tokens.front().begin != 0 || tokens.front().end > input.size() || tokens.front().subtreeEnd > tokens.size())
    throw Error("invalid token stream", 0);
  if (options.sourceValidation == SourceValidation::Identity && (input.data() != source.data() || input.size() != source.size()))
    throw Error("tokens belong to a different buffer", 0);
  if (options.sourceValidation == SourceValidation::Content) {
    if (!hasFingerprint)
      throw Error("tokenized document has no content fingerprint", 0);
    if (fingerprint != bufferFingerprint(input))
      throw Error("tokens belong to different buffer content", 0);
  }
  auto parseOptions = options;
  parseOptions.format = format;
  return Reader(input, parseOptions, nullptr).root();
}
} // namespace

Tag parse(const TokenizedDocument &document, const ParseOptions &options) { return parseTokenized(document.source, document.source, document.tokens, document.fingerprint, document.hasFingerprint, document.format, options); }
Tag parse(const TokenizedView &document, const ParseOptions &options) { return parseTokenized(document.source, document.source, document.tokens, document.fingerprint, document.hasFingerprint, document.format, options); }
Tag parse(std::span<const std::byte> input, const TokenizedDocument &document, const ParseOptions &options) { return parseTokenized(input, document.source, document.tokens, document.fingerprint, document.hasFingerprint, document.format, options); }
Tag parse(std::span<const std::byte> input, const TokenizedView &document, const ParseOptions &options) { return parseTokenized(input, document.source, document.tokens, document.fingerprint, document.hasFingerprint, document.format, options); }
Buffer serialize(const Tag &root, BinaryFormat format) {
  if (root.type == Type::End)
    throw std::invalid_argument("TAG_End cannot be serialized as a root tag");
  if (format == BinaryFormat::Network && root.type != Type::Compound)
    throw std::invalid_argument("Network NBT root must be TAG_Compound");
  return Writer().run(root, format == BinaryFormat::File);
}
Buffer compress(std::span<const std::byte> input, Compression c) {
  if (c == Compression::None)
    return {input.begin(), input.end()};
  if (c == Compression::Auto)
    throw std::invalid_argument("Auto is invalid for compression");
  return zcode(input, c == Compression::Gzip ? 31 : 15, true);
}
Buffer decompress(std::span<const std::byte> input, Compression c) {
  if (c == Compression::None)
    return {input.begin(), input.end()};
  return zcode(input, c == Compression::Gzip ? 31 : c == Compression::Zlib ? 15 : 47, false);
}
Tag load(const std::filesystem::path &path, Compression c, const ParseOptions &options) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error("cannot open NBT file");
  const auto length = f.tellg();
  if (length < 0)
    throw std::runtime_error("cannot determine NBT file size");
  Buffer b(static_cast<std::size_t>(length));
  f.seekg(0);
  if (!b.empty() && !f.read(reinterpret_cast<char *>(b.data()), length))
    throw std::runtime_error("cannot read NBT file");
  if (c == Compression::Auto && b.size() >= 2) {
    auto a = std::to_integer<unsigned>(b[0]), d = std::to_integer<unsigned>(b[1]);
    if (!(a == 0x1f && d == 0x8b) && !((a & 0x0f) == 8 && ((a << 8) + d) % 31 == 0))
      c = Compression::None;
  }
  auto raw = decompress(b, c);
  return parse(raw, options);
}
void save(const std::filesystem::path &path, const Tag &root, Compression c) {
  auto raw = serialize(root);
  auto data = compress(raw, c);
  std::ofstream f(path, std::ios::binary);
  if (!f || !f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size())))
    throw std::runtime_error("cannot write NBT file");
}

Tag clone(const Tag &tag) { return tag; }
bool map(Tag &root, const Visitor &v) {
  if (!v(root))
    return false;
  if (container(root))
    for (auto &c : children(root))
      if (!map(c, v))
        return false;
  return true;
}
bool map(const Tag &root, const ConstVisitor &v) {
  if (!v(root))
    return false;
  if (container(root))
    for (const auto &c : children(root))
      if (!map(c, v))
        return false;
  return true;
}
std::optional<Tag> filter(const Tag &root, const Predicate &p) {
  if (!p(root))
    return std::nullopt;
  Tag copy = root;
  if (container(copy)) {
    auto &v = children(copy);
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](Tag &c) {
                             auto x = filter(c, p);
                             if (x)
                               c = std::move(*x);
                             return !x;
                           }),
            v.end());
  }
  return copy;
}
void filterInPlace(Tag &root, const Predicate &p) {
  if (!container(root))
    return;
  auto &v = children(root);
  v.erase(std::remove_if(v.begin(), v.end(),
                         [&](Tag &c) {
                           if (!p(c))
                             return true;
                           filterInPlace(c, p);
                           return false;
                         }),
          v.end());
}
Tag *find(Tag &root, const Predicate &p) {
  if (p(root))
    return &root;
  if (container(root))
    for (auto &c : children(root))
      if (auto *r = find(c, p))
        return r;
  return nullptr;
}
const Tag *find(const Tag &root, const Predicate &p) {
  if (p(root))
    return &root;
  if (container(root))
    for (const auto &c : children(root))
      if (auto *r = find(c, p))
        return r;
  return nullptr;
}
Tag *findByName(Tag &root, std::string_view n) {
  return find(root, [&](const Tag &t) { return t.name == n; });
}
const Tag *findByName(const Tag &root, std::string_view n) {
  return find(root, [&](const Tag &t) { return t.name == n; });
}
Tag *at(Tag &t, std::size_t i) { return container(t) && i < children(t).size() ? &children(t)[i] : nullptr; }
const Tag *at(const Tag &t, std::size_t i) { return container(t) && i < children(t).size() ? &children(t)[i] : nullptr; }
Tag *findByPath(Tag &root, std::string_view path) {
  Tag *cur = &root;
  size_t start = 0;
  if (auto dot = path.find('.'); (dot == std::string_view::npos ? path : path.substr(0, dot)) == root.name)
    start = dot == std::string_view::npos ? path.size() : dot + 1;
  while (start < path.size()) {
    auto end = path.find('.', start);
    auto part = path.substr(start, end - start);
    if (!container(*cur))
      return nullptr;
    auto &v = children(*cur);
    auto it = std::find_if(v.begin(), v.end(), [&](Tag &x) { return x.name == part; });
    if (it == v.end())
      return nullptr;
    cur = &*it;
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return cur;
}
const Tag *findByPath(const Tag &root, std::string_view path) { return findByPath(const_cast<Tag &>(root), path); }
std::size_t size(const Tag &root) {
  std::size_t n = 1;
  if (container(root))
    for (const auto &c : children(root))
      n += size(c);
  return n;
}
bool equivalent(const Tag &a, const Tag &b, double e) {
  if (a.type != b.type || a.name != b.name || a.elementType != b.elementType)
    return false;
  if (container(a)) {
    const auto &x = children(a);
    const auto &y = children(b);
    return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [&](const Tag &l, const Tag &r) { return equivalent(l, r, e); });
  }
  switch (a.type) {
  case Type::End:
    return true;
  case Type::Byte:
    return a.as<Byte>() == b.as<Byte>();
  case Type::Short:
    return a.as<std::int16_t>() == b.as<std::int16_t>();
  case Type::Int:
    return a.as<std::int32_t>() == b.as<std::int32_t>();
  case Type::Long:
    return a.as<std::int64_t>() == b.as<std::int64_t>();
  case Type::Float:
    return std::abs(a.as<float>() - b.as<float>()) <= e;
  case Type::Double:
    return std::abs(a.as<double>() - b.as<double>()) <= e;
  case Type::ByteArray:
    return a.as<ByteArray>() == b.as<ByteArray>();
  case Type::String:
    return a.as<std::string>() == b.as<std::string>();
  case Type::IntArray:
    return a.as<IntArray>() == b.as<IntArray>();
  case Type::LongArray:
    return a.as<LongArray>() == b.as<LongArray>();
  case Type::List:
  case Type::Compound:
    return false;
  }
  return false;
}
Tag parseSnbt(std::string_view input, const ParseOptions &options) { return SnbtParser(input, options).run(); }
std::string toSnbt(const Tag &root, bool pretty) {
  std::ostringstream out;
  snbt(root, out, pretty, 0, true);
  return out.str();
}
std::string_view typeName(Type t) noexcept {
  static constexpr std::string_view names[] = {"TAG_End", "TAG_Byte", "TAG_Short", "TAG_Int", "TAG_Long", "TAG_Float", "TAG_Double", "TAG_Byte_Array", "TAG_String", "TAG_List", "TAG_Compound", "TAG_Int_Array", "TAG_Long_Array"};
  auto i = static_cast<size_t>(t);
  return i < std::size(names) ? names[i] : "TAG_Unknown";
}

Builder::Builder(std::string name) : root_(Tag::compound(std::move(name))), stack_{&root_} {}
Builder &Builder::add(Tag tag) {
  if (stack_.empty() || !container(*stack_.back()))
    throw std::logic_error("builder has no open container");
  if (stack_.back()->type == Type::List) {
    if (tag.type != stack_.back()->elementType)
      throw std::invalid_argument("list element type mismatch");
    tag.name.clear();
  }
  children(*stack_.back()).push_back(std::move(tag));
  return *this;
}
Builder &Builder::beginCompound(std::string name) {
  add(Tag::compound(std::move(name)));
  auto &v = children(*stack_.back());
  stack_.push_back(&v.back());
  return *this;
}
Builder &Builder::beginList(std::string name, Type type) {
  add(Tag::list(std::move(name), type));
  auto &v = children(*stack_.back());
  stack_.push_back(&v.back());
  return *this;
}
Builder &Builder::end() {
  if (stack_.size() <= 1)
    throw std::logic_error("cannot close root compound");
  stack_.pop_back();
  return *this;
}
Tag Builder::build() const {
  if (stack_.size() != 1)
    throw std::logic_error("builder contains unclosed containers");
  return root_;
}

} // namespace nbt
